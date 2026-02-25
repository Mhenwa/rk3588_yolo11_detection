#include "modules/source/rtsp_source.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <gst/gst.h>

#include "core/log/app_log.h"
#include "modules/source/capturer.h"

namespace modules
{
    namespace source
    {

        namespace
        {
            constexpr size_t kMaxQueuedFrames = 4;
            constexpr int kReadTimeoutMs = 1500;

            std::once_flag g_gst_init_once;
            std::mutex g_route_mu;
            std::unordered_map<int, RtspSource *> g_channel_routes;
            std::atomic<int> g_next_channel_id{0};

            std::string normalize_codec(const std::string &codec)
            {
                std::string lower = codec;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower == "h265" || lower == "hevc")
                {
                    return "h265";
                }
                return "h264";
            }

            bool equals_ignore_case(const char *lhs, const char *rhs)
            {
                if (!lhs || !rhs)
                {
                    return false;
                }
                while (*lhs && *rhs)
                {
                    const unsigned char l = static_cast<unsigned char>(*lhs++);
                    const unsigned char r = static_cast<unsigned char>(*rhs++);
                    if (std::tolower(l) != std::tolower(r))
                    {
                        return false;
                    }
                }
                return *lhs == '\0' && *rhs == '\0';
            }

            bool convert_bgr_frame(const RtspSource::RawFrame &raw, SourceFrame *out)
            {
                if (!out)
                {
                    return false;
                }
                const int width = raw.desc.width;
                const int height = raw.desc.height;
                const int stride = raw.desc.horStride > 0 ? raw.desc.horStride : width;
                const int rows = raw.desc.verStride > 0 ? raw.desc.verStride : height;
                if (width <= 0 || height <= 0 || stride < width || rows < height)
                {
                    return false;
                }

                const size_t row_bytes = static_cast<size_t>(stride) * 3;
                const size_t required = row_bytes * static_cast<size_t>(rows);
                if (raw.data.size() < required)
                {
                    return false;
                }

                cv::Mat bgr_padded(rows, stride, CV_8UC3,
                                   const_cast<unsigned char *>(raw.data.data()), row_bytes);
                cv::Rect roi(0, 0, width, height);
                out->frame = bgr_padded(roi).clone();
                out->data.clear();
                out->format = SourceFrameFormat::kBgr;
                out->width = width;
                out->height = height;
                return !out->frame.empty();
            }

            bool convert_nv_frame(const RtspSource::RawFrame &raw, int cv_code, SourceFrame *out)
            {
                if (!out)
                {
                    return false;
                }
                const int width = raw.desc.width;
                const int height = raw.desc.height;
                const int stride = raw.desc.horStride > 0 ? raw.desc.horStride : width;
                const int y_rows = raw.desc.verStride > 0 ? raw.desc.verStride : height;
                if (width <= 0 || height <= 0 || stride < width || y_rows < height ||
                    (stride % 2) != 0 || (width % 2) != 0 || (height % 2) != 0)
                {
                    return false;
                }

                const size_t y_size = static_cast<size_t>(stride) * static_cast<size_t>(y_rows);
                const size_t required = y_size + y_size / 2;
                if (raw.data.size() < required)
                {
                    return false;
                }

                cv::Mat y_full(y_rows, stride, CV_8UC1,
                               const_cast<unsigned char *>(raw.data.data()), stride);
                cv::Mat uv_full(y_rows / 2, stride / 2, CV_8UC2,
                                const_cast<unsigned char *>(raw.data.data()) + y_size, stride);

                cv::Rect y_roi(0, 0, width, height);
                cv::Rect uv_roi(0, 0, width / 2, height / 2);
                cv::Mat y = y_full(y_roi);
                cv::Mat uv = uv_full(uv_roi);

                cv::cvtColorTwoPlane(y, uv, out->frame, cv_code);
                out->data.clear();
                out->format = SourceFrameFormat::kBgr;
                out->width = width;
                out->height = height;
                return !out->frame.empty();
            }
        } // namespace

        RtspSource::RtspSource(std::string url, std::string codec)
            : url_(std::move(url)), codec_(std::move(codec))
        {
        }

        RtspSource::~RtspSource()
        {
            Close();
        }

        bool RtspSource::Open()
        {
            Close();
            std::call_once(g_gst_init_once, []() {
                gst_init(nullptr, nullptr);
            });
            return OpenCaptureWithCodec(url_, codec_);
        }

        void RtspSource::Close()
        {
            int old_channel = -1;
            {
                std::lock_guard<std::mutex> lk(mu_);
                stop_ = true;
                opened_ = false;
                old_channel = channel_id_;
                channel_id_ = -1;
                frames_.clear();
            }
            cv_.notify_all();

            if (old_channel >= 0)
            {
                std::lock_guard<std::mutex> lk(g_route_mu);
                g_channel_routes.erase(old_channel);
            }
            capturer_.reset();
        }

        bool RtspSource::Read(SourceFrame *out)
        {
            if (!out)
            {
                return false;
            }

            RawFrame raw;
            {
                std::unique_lock<std::mutex> lk(mu_);
                if (!opened_)
                {
                    return false;
                }
                cv_.wait_for(lk, std::chrono::milliseconds(kReadTimeoutMs), [this]() {
                    return stop_ || !frames_.empty();
                });
                if (stop_ || !opened_ || frames_.empty())
                {
                    return false;
                }

                raw = std::move(frames_.back());
                frames_.clear();
            }

            out->capture_tp = raw.capture_tp;
            out->frame.release();
            out->data.clear();
            out->width = 0;
            out->height = 0;
            out->format = SourceFrameFormat::kUnknown;

            if (equals_ignore_case(raw.desc.fmt, "BGR"))
            {
                return convert_bgr_frame(raw, out);
            }
            if (equals_ignore_case(raw.desc.fmt, "NV12"))
            {
                return convert_nv_frame(raw, cv::COLOR_YUV2BGR_NV12, out);
            }
            if (equals_ignore_case(raw.desc.fmt, "NV21"))
            {
                return convert_nv_frame(raw, cv::COLOR_YUV2BGR_NV21, out);
            }
            LOGE("rtsp unsupported frame fmt=%s\n", raw.desc.fmt);
            return false;
        }

        bool RtspSource::OpenCaptureWithCodec(const std::string &url,
                                              const std::string &codec)
        {
            const std::string use_codec = normalize_codec(codec);
            const int chn_id = g_next_channel_id.fetch_add(1);

            SrcCfg_t cfg = {};
            cfg.srcType = "rtsp";
            cfg.loaction = url.c_str();
            cfg.videoEncType = use_codec.c_str();
            cfg.audioEncType = "null";

            auto capturer = std::make_unique<Capturer>(chn_id, cfg);
            if (!capturer)
            {
                return false;
            }

            {
                std::lock_guard<std::mutex> lk(mu_);
                stop_ = false;
                opened_ = true;
                channel_id_ = chn_id;
                frames_.clear();
            }

            {
                std::lock_guard<std::mutex> lk(g_route_mu);
                g_channel_routes[chn_id] = this;
            }

            if (capturer->init() != 0 || capturer->IsInited() == 0)
            {
                Close();
                return false;
            }

            capturer_ = std::move(capturer);
            LOGI("rtsp opened: ch=%d codec=%s url=%s\n", chn_id, use_codec.c_str(), url.c_str());
            return true;
        }

        int RtspSource::PushFrame(char *imgData, const RtspImgDesc_t &imgDesc)
        {
            if (!imgData || imgDesc.dataSize <= 0)
            {
                return -1;
            }

            RawFrame raw = {};
            raw.desc = imgDesc;
            raw.capture_tp = std::chrono::steady_clock::now();
            raw.data.resize(static_cast<size_t>(imgDesc.dataSize));
            std::memcpy(raw.data.data(), imgData, raw.data.size());

            {
                std::lock_guard<std::mutex> lk(mu_);
                if (stop_ || !opened_)
                {
                    return -1;
                }
                if (frames_.size() >= kMaxQueuedFrames)
                {
                    frames_.pop_front();
                }
                frames_.push_back(std::move(raw));
            }
            cv_.notify_one();
            return 0;
        }

        int rtspVideoOutHandle(char *imgData, RtspImgDesc_t imgDesc)
        {
            std::lock_guard<std::mutex> lk(g_route_mu);
            const auto it = g_channel_routes.find(imgDesc.chnId);
            if (it == g_channel_routes.end() || !it->second)
            {
                return -1;
            }
            return it->second->PushFrame(imgData, imgDesc);
        }

    } // namespace source
} // namespace modules
