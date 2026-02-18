#include "modules/source/mipi_source.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/log/app_log.h"

namespace modules
{
    namespace source
    {

        namespace
        {
            constexpr int kNv12PlaneCount = 2;
            constexpr int kFallbackWidth = 640;
            constexpr int kFallbackHeight = 480;

            void log_errno(const char *action)
            {
                LOGE("%s: %s\n", action, strerror(errno));
            }

            std::string fourcc_to_string(uint32_t fmt)
            {
                char fourcc[5];
                fourcc[0] = static_cast<char>(fmt & 0xff);
                fourcc[1] = static_cast<char>((fmt >> 8) & 0xff);
                fourcc[2] = static_cast<char>((fmt >> 16) & 0xff);
                fourcc[3] = static_cast<char>((fmt >> 24) & 0xff);
                fourcc[4] = '\0';
                return std::string(fourcc);
            }

            std::string to_lower(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char ch)
                               {
                                   return static_cast<char>(std::tolower(ch));
                               });
                return value;
            }
        } // namespace

        class MipiV4L2Camera
        {
        public:
            static constexpr int kDefaultBufferCount = 4;
            static constexpr int kMaxBufferCount = 8;

            MipiV4L2Camera(std::string device,
                           int width,
                           int height,
                           int buffer_count,
                           double target_fps,
                           std::string format,
                           bool allow_format_fallback);
            ~MipiV4L2Camera();

            bool init();
            bool start();
            bool stop();
            bool getFrame(SourceFrame *frame);

        private:
            struct V4L2Buffer
            {
                void *start = nullptr;
                size_t length = 0;
            };

            int fd_;
            std::string device_;
            int width_;
            int height_;
            int frame_width_;
            int frame_height_;
            int buffer_count_;
            double target_fps_;
            std::string format_;
            bool allow_format_fallback_;
            double actual_fps_;
            bool streaming_;
            bool is_mplane_;
            uint32_t pixel_format_;
            int plane_count_;
            std::vector<V4L2Buffer> buffers_;
        };

        MipiV4L2Camera::MipiV4L2Camera(std::string device,
                                       int width,
                                       int height,
                                       int buffer_count,
                                       double target_fps,
                                       std::string format,
                                       bool allow_format_fallback)
            : fd_(-1),
              device_(std::move(device)),
              width_(width),
              height_(height),
              frame_width_(width),
              frame_height_(height),
              buffer_count_(buffer_count),
              target_fps_(target_fps),
              format_(std::move(format)),
              allow_format_fallback_(allow_format_fallback),
              actual_fps_(0.0),
              streaming_(false),
              is_mplane_(false),
              pixel_format_(0),
              plane_count_(1)
        {
        }

        MipiV4L2Camera::~MipiV4L2Camera()
        {
            stop();
            for (size_t i = 0; i < buffers_.size(); ++i)
            {
                if (buffers_[i].start && buffers_[i].start != MAP_FAILED &&
                    buffers_[i].length > 0)
                {
                    munmap(buffers_[i].start, buffers_[i].length);
                }
            }
            if (fd_ >= 0)
            {
                close(fd_);
            }
        }

        bool MipiV4L2Camera::init()
        {
            fd_ = open(device_.c_str(), O_RDWR);
            if (fd_ < 0)
            {
                log_errno("Open device");
                return false;
            }

            struct v4l2_capability cap = {};
            if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0)
            {
                log_errno("Query capabilities");
                return false;
            }
            uint32_t caps = cap.capabilities;
            if (caps & V4L2_CAP_DEVICE_CAPS)
                caps = cap.device_caps;
            const bool cap_mplane = (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
            const bool cap_capture = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
            if (!cap_mplane && !cap_capture)
            {
                LOGE("MIPI source requires capture or mplane capability\n");
                return false;
            }
            if (!(caps & V4L2_CAP_STREAMING))
            {
                LOGE("Device does not support streaming\n");
                return false;
            }

            std::string desired_format = to_lower(format_);
            if (desired_format == "mjpg")
                desired_format = "mjpeg";
            if (desired_format.empty())
                desired_format = "auto";

            const bool format_auto = desired_format == "auto";
            const bool want_nv12 = desired_format == "nv12";
            const bool want_yuyv = desired_format == "yuyv";
            const bool want_mjpeg = desired_format == "mjpeg";
            if (!format_auto && !want_nv12 && !want_yuyv && !want_mjpeg)
            {
                LOGE("Unsupported MIPI format requested: %s\n", format_.c_str());
                return false;
            }
            if (want_mjpeg && !allow_format_fallback_)
            {
                LOGE("MIPI does not support MJPEG without fallback\n");
                return false;
            }

            auto push_unique = [](std::vector<uint32_t> *list, uint32_t fmt)
            {
                if (std::find(list->begin(), list->end(), fmt) == list->end())
                {
                    list->push_back(fmt);
                }
            };

            std::vector<uint32_t> mplane_formats;
            std::vector<uint32_t> capture_formats;

            if (cap_mplane && (format_auto || want_nv12 || want_mjpeg ||
                               (want_yuyv && allow_format_fallback_)))
            {
                push_unique(&mplane_formats, V4L2_PIX_FMT_NV12);
            }
            if (cap_capture)
            {
                if (format_auto)
                {
                    push_unique(&capture_formats, V4L2_PIX_FMT_NV12);
                    push_unique(&capture_formats, V4L2_PIX_FMT_YUYV);
                }
                else if (want_nv12)
                {
                    push_unique(&capture_formats, V4L2_PIX_FMT_NV12);
                    if (allow_format_fallback_)
                        push_unique(&capture_formats, V4L2_PIX_FMT_YUYV);
                }
                else if (want_yuyv)
                {
                    push_unique(&capture_formats, V4L2_PIX_FMT_YUYV);
                    if (allow_format_fallback_)
                        push_unique(&capture_formats, V4L2_PIX_FMT_NV12);
                }
                else if (want_mjpeg && allow_format_fallback_)
                {
                    push_unique(&capture_formats, V4L2_PIX_FMT_NV12);
                    push_unique(&capture_formats, V4L2_PIX_FMT_YUYV);
                }
            }

            auto try_set_mplane = [&](uint32_t pixfmt, int width, int height) -> bool
            {
                struct v4l2_format fmt = {};
                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                fmt.fmt.pix_mp.width = width;
                fmt.fmt.pix_mp.height = height;
                fmt.fmt.pix_mp.pixelformat = pixfmt;
                fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
                fmt.fmt.pix_mp.num_planes = kNv12PlaneCount;
                if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
                {
                    return false;
                }
                is_mplane_ = true;
                pixel_format_ = fmt.fmt.pix_mp.pixelformat;
                plane_count_ = fmt.fmt.pix_mp.num_planes;
                frame_width_ = fmt.fmt.pix_mp.width;
                frame_height_ = fmt.fmt.pix_mp.height;
                return true;
            };

            auto try_set_capture = [&](uint32_t pixfmt, int width, int height) -> bool
            {
                struct v4l2_format fmt = {};
                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width = width;
                fmt.fmt.pix.height = height;
                fmt.fmt.pix.pixelformat = pixfmt;
                fmt.fmt.pix.field = V4L2_FIELD_NONE;
                if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
                {
                    return false;
                }
                is_mplane_ = false;
                pixel_format_ = fmt.fmt.pix.pixelformat;
                plane_count_ = 1;
                frame_width_ = fmt.fmt.pix.width;
                frame_height_ = fmt.fmt.pix.height;
                return true;
            };

            auto try_mplane_formats = [&](int width, int height) -> bool
            {
                for (size_t i = 0; i < mplane_formats.size(); ++i)
                {
                    if (try_set_mplane(mplane_formats[i], width, height))
                        return true;
                }
                return false;
            };
            auto try_capture_formats = [&](int width, int height) -> bool
            {
                for (size_t i = 0; i < capture_formats.size(); ++i)
                {
                    if (try_set_capture(capture_formats[i], width, height))
                        return true;
                }
                return false;
            };

            const bool prefer_mplane = !want_yuyv;
            auto try_with_size = [&](int width, int height) -> bool
            {
                bool ok = false;
                if (prefer_mplane)
                {
                    if (cap_mplane)
                        ok = try_mplane_formats(width, height);
                    if (!ok && cap_capture)
                        ok = try_capture_formats(width, height);
                }
                else
                {
                    if (cap_capture)
                        ok = try_capture_formats(width, height);
                    if (!ok && cap_mplane)
                        ok = try_mplane_formats(width, height);
                }
                return ok;
            };

            bool format_ok = try_with_size(width_, height_);
            if (!format_ok && (width_ != kFallbackWidth || height_ != kFallbackHeight))
            {
                format_ok = try_with_size(kFallbackWidth, kFallbackHeight);
            }
            if (!format_ok)
            {
                if (!format_auto)
                {
                    LOGE("Requested MIPI format not supported: %s\n", desired_format.c_str());
                }
                log_errno("Set format");
                return false;
            }

            if (is_mplane_ && (plane_count_ < 1 || plane_count_ > kNv12PlaneCount))
            {
                LOGE("Unsupported mplane count: %d\n", plane_count_);
                return false;
            }

            const double requested_fps = target_fps_ > 0.0 ? target_fps_ : 30.0;
            if (requested_fps > 0.0)
            {
                struct v4l2_streamparm parm = {};
                parm.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                       : V4L2_BUF_TYPE_VIDEO_CAPTURE;
                const double rounded = std::round(requested_fps);
                if (std::fabs(requested_fps - rounded) < 1e-3)
                {
                    parm.parm.capture.timeperframe.numerator = 1;
                    parm.parm.capture.timeperframe.denominator =
                        static_cast<unsigned int>(rounded);
                }
                else
                {
                    const double base = 1000.0;
                    double num = base / requested_fps;
                    if (num < 1.0)
                        num = 1.0;
                    parm.parm.capture.timeperframe.numerator =
                        static_cast<unsigned int>(std::lround(num));
                    parm.parm.capture.timeperframe.denominator =
                        static_cast<unsigned int>(base);
                }
                if (ioctl(fd_, VIDIOC_S_PARM, &parm) < 0)
                {
                    log_errno("Set frame rate");
                }
            }

            struct v4l2_streamparm actual_parm = {};
            actual_parm.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                          : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd_, VIDIOC_G_PARM, &actual_parm) < 0)
            {
                log_errno("Get frame rate");
            }
            else if (actual_parm.parm.capture.timeperframe.numerator > 0)
            {
                actual_fps_ = static_cast<double>(
                                  actual_parm.parm.capture.timeperframe.denominator) /
                              static_cast<double>(
                                  actual_parm.parm.capture.timeperframe.numerator);
            }

            struct v4l2_requestbuffers req = {};
            int req_count = buffer_count_;
            if (req_count <= 0)
                req_count = kDefaultBufferCount;
            if (req_count > kMaxBufferCount)
            {
                LOGW("Requested buffer count %d too large, clamp to %d\n",
                     req_count, kMaxBufferCount);
                req_count = kMaxBufferCount;
            }
            req.count = req_count;
            req.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                  : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0)
            {
                log_errno("Request buffers");
                return false;
            }
            if (req.count < 1)
            {
                LOGE("No buffers allocated for %s\n", device_.c_str());
                return false;
            }
            buffer_count_ = static_cast<int>(req.count);
            buffers_.assign(buffer_count_, {});

            {
                char summary[384];
                int offset = snprintf(summary, sizeof(summary),
                                      "MIPI V4L2: %s %s target fmt=%s size=%dx%d fps=%.2f"
                                      " -> actual fmt=%s size=%dx%d",
                                      device_.c_str(),
                                      is_mplane_ ? "mplane" : "capture",
                                      desired_format.c_str(),
                                      width_, height_,
                                      requested_fps,
                                      fourcc_to_string(pixel_format_).c_str(),
                                      frame_width_, frame_height_);
                if (actual_fps_ > 0.0)
                {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                                       " fps=%.2f", actual_fps_);
                }
                else
                {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                                       " fps=unknown");
                }
                if (is_mplane_)
                {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                                       " planes=%d", plane_count_);
                }
                offset += snprintf(summary + offset, sizeof(summary) - offset,
                                   " buffers=%d", buffer_count_);
                LOGI("%s\n", summary);
            }

            for (int i = 0; i < buffer_count_; ++i)
            {
                struct v4l2_buffer buf = {};
                buf.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                      : V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                if (is_mplane_)
                {
                    struct v4l2_plane planes[kNv12PlaneCount] = {};
                    buf.length = plane_count_;
                    buf.m.planes = planes;
                    if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
                    {
                        log_errno("Query buffer");
                        return false;
                    }
                    size_t total_len = 0;
                    for (int p = 0; p < plane_count_; ++p)
                    {
                        total_len += buf.m.planes[p].length;
                    }
                    buffers_[i].length = total_len;
                    buffers_[i].start = mmap(NULL, total_len, PROT_READ | PROT_WRITE,
                                             MAP_SHARED, fd_, buf.m.planes[0].m.mem_offset);
                    if (buffers_[i].start == MAP_FAILED)
                    {
                        log_errno("mmap");
                        return false;
                    }
                    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
                    {
                        log_errno("Initial queue buffer");
                        return false;
                    }
                }
                else
                {
                    if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
                    {
                        log_errno("Query buffer");
                        return false;
                    }
                    buffers_[i].length = buf.length;
                    buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                             MAP_SHARED, fd_, buf.m.offset);
                    if (buffers_[i].start == MAP_FAILED)
                    {
                        log_errno("mmap");
                        return false;
                    }
                    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
                    {
                        log_errno("Initial queue buffer");
                        return false;
                    }
                }
            }

            return true;
        }

        bool MipiV4L2Camera::start()
        {
            enum v4l2_buf_type type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                                 : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0)
            {
                log_errno("Stream on");
                return false;
            }
            streaming_ = true;
            return true;
        }

        bool MipiV4L2Camera::stop()
        {
            if (fd_ < 0 || !streaming_)
                return true;
            enum v4l2_buf_type type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                                 : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
            {
                log_errno("Stream off");
                return false;
            }
            streaming_ = false;
            return true;
        }

        bool MipiV4L2Camera::getFrame(SourceFrame *frame)
        {
            if (!frame)
                return false;

            struct v4l2_buffer buf = {};
            buf.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                  : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            bool ok = true;
            frame->frame.release();
            frame->data.clear();
            frame->width = frame_width_;
            frame->height = frame_height_;
            frame->format = SourceFrameFormat::kUnknown;

            const size_t expected_nv12 =
                static_cast<size_t>(frame_height_) *
                static_cast<size_t>(frame_width_) * 3 / 2;
            const size_t expected_yuyv =
                static_cast<size_t>(frame_height_) *
                static_cast<size_t>(frame_width_) * 2;

            if (is_mplane_)
            {
                struct v4l2_plane planes[kNv12PlaneCount] = {};
                buf.length = plane_count_;
                buf.m.planes = planes;
                if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0)
                {
                    log_errno("Dequeue buffer");
                    return false;
                }
                if (buf.index >= buffers_.size())
                {
                    LOGE("Invalid buffer index: %u\n", buf.index);
                    return false;
                }
                if (pixel_format_ == V4L2_PIX_FMT_NV12)
                {
                    size_t used = 0;
                    for (int p = 0; p < plane_count_; ++p)
                    {
                        used += static_cast<size_t>(planes[p].bytesused);
                    }
                    if (used < expected_nv12)
                    {
                        LOGE("mplane NV12 bytesused too small: %zu < %zu\n", used, expected_nv12);
                        ok = false;
                    }
                    else
                    {
                        unsigned char *y_plane = static_cast<unsigned char *>(buffers_[buf.index].start);
                        frame->data.assign(y_plane, y_plane + expected_nv12);
                        frame->format = SourceFrameFormat::kNv12;
                    }
                }
                else
                {
                    LOGE("Unsupported mplane format: %s\n",
                         fourcc_to_string(pixel_format_).c_str());
                    ok = false;
                }
            }
            else
            {
                if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0)
                {
                    log_errno("Dequeue buffer");
                    return false;
                }
                if (buf.index >= buffers_.size())
                {
                    LOGE("Invalid buffer index: %u\n", buf.index);
                    return false;
                }
                unsigned char *data =
                    static_cast<unsigned char *>(buffers_[buf.index].start);
                if (pixel_format_ == V4L2_PIX_FMT_NV12)
                {
                    const size_t used = buf.bytesused > 0 ? static_cast<size_t>(buf.bytesused) : expected_nv12;
                    if (used < expected_nv12)
                    {
                        LOGE("NV12 bytesused too small: %zu < %zu\n", used, expected_nv12);
                        ok = false;
                    }
                    else
                    {
                        frame->data.assign(data, data + expected_nv12);
                        frame->format = SourceFrameFormat::kNv12;
                    }
                }
                else if (pixel_format_ == V4L2_PIX_FMT_YUYV)
                {
                    const size_t used = buf.bytesused > 0 ? static_cast<size_t>(buf.bytesused) : expected_yuyv;
                    if (used < expected_yuyv)
                    {
                        LOGE("YUYV bytesused too small: %zu < %zu\n", used, expected_yuyv);
                        ok = false;
                    }
                    else
                    {
                        frame->data.assign(data, data + expected_yuyv);
                        frame->format = SourceFrameFormat::kYuyv;
                    }
                }
                else
                {
                    LOGE("Unsupported capture format: %s\n",
                         fourcc_to_string(pixel_format_).c_str());
                    ok = false;
                }
            }

            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            {
                log_errno("Requeue buffer");
                return false;
            }

            return ok;
        }

        MipiSource::MipiSource(std::string device,
                               int width,
                               int height,
                               int buffers,
                               double fps,
                               std::string format)
            : device_(std::move(device)),
              width_(width),
              height_(height),
              buffers_(buffers),
              fps_(fps),
              format_(std::move(format))
        {
        }

        MipiSource::~MipiSource() = default;

        bool MipiSource::Open()
        {
            const int cam_w = width_ > 0 ? width_ : kFallbackWidth;
            const int cam_h = height_ > 0 ? height_ : kFallbackHeight;
            const int cam_buffers =
                buffers_ > 0 ? buffers_ : MipiV4L2Camera::kDefaultBufferCount;
            const double cam_fps = fps_ > 0.0 ? fps_ : 30.0;
            const std::string cam_format = format_.empty() ? "auto" : format_;

            camera_ = std::make_unique<MipiV4L2Camera>(
                device_, cam_w, cam_h, cam_buffers, cam_fps, cam_format, true);
            if (!camera_->init() || !camera_->start())
            {
                camera_.reset();
                return false;
            }
            return true;
        }

        void MipiSource::Close()
        {
            camera_.reset();
        }

        bool MipiSource::Read(SourceFrame *out)
        {
            if (!out || !camera_)
                return false;
            out->capture_tp = std::chrono::steady_clock::now();
            if (!camera_->getFrame(out))
            {
                return false;
            }
            if (out->format == SourceFrameFormat::kBgr)
            {
                return !out->frame.empty();
            }
            return !out->data.empty();
        }

    } // namespace source
} // namespace modules
