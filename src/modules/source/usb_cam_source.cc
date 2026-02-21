#include "modules/source/usb_cam_source.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "core/log/app_log.h"
#include "core/utils/string_utils.h"

namespace modules
{
    namespace source
    {

        namespace
        {
            constexpr int kFallbackWidth = 640;
            constexpr int kFallbackHeight = 480;
            constexpr int kDefaultCameraBufferCount = 4;

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

        } // namespace

        class UsbV4L2Camera
        {
        public:
            static constexpr int kDefaultBufferCount = 4;
            static constexpr int kMaxBufferCount = 8;

            UsbV4L2Camera(std::string device,
                          int width,
                          int height,
                          int buffer_count,
                          double target_fps,
                          std::string format);
            ~UsbV4L2Camera();

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
            double actual_fps_;
            bool streaming_;
            uint32_t pixel_format_;
            std::vector<V4L2Buffer> buffers_;
        };

        UsbV4L2Camera::UsbV4L2Camera(std::string device,
                                     int width,
                                     int height,
                                     int buffer_count,
                                     double target_fps,
                                     std::string format)
            : fd_(-1),
              device_(std::move(device)),
              width_(width),
              height_(height),
              frame_width_(width),
              frame_height_(height),
              buffer_count_(buffer_count),
              target_fps_(target_fps),
              format_(std::move(format)),
              actual_fps_(0.0),
              streaming_(false),
              pixel_format_(0)
        {
        }

        UsbV4L2Camera::~UsbV4L2Camera()
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

        bool UsbV4L2Camera::init()
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
            if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
            {
                LOGE("USB source requires V4L2 capture capability\n");
                return false;
            }
            if (!(caps & V4L2_CAP_STREAMING))
            {
                LOGE("Device does not support streaming\n");
                return false;
            }

            std::string desired_format = core::utils::to_lower(format_);
            if (desired_format == "mjpg")
                desired_format = "mjpeg";
            if (desired_format.empty())
                desired_format = "auto";
            const bool format_auto = desired_format == "auto";
            const bool want_mjpeg = desired_format == "mjpeg";
            const bool want_yuyv = desired_format == "yuyv";
            if (!format_auto && !want_mjpeg && !want_yuyv)
            {
                LOGE("Unsupported USB format requested: %s\n", format_.c_str());
                return false;
            }

            std::vector<uint32_t> capture_formats;
            if (format_auto)
            {
                capture_formats.push_back(V4L2_PIX_FMT_MJPEG);
                capture_formats.push_back(V4L2_PIX_FMT_YUYV);
            }
            else if (want_mjpeg)
            {
                capture_formats.push_back(V4L2_PIX_FMT_MJPEG);
            }
            else
            {
                capture_formats.push_back(V4L2_PIX_FMT_YUYV);
            }

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
                pixel_format_ = fmt.fmt.pix.pixelformat;
                frame_width_ = fmt.fmt.pix.width;
                frame_height_ = fmt.fmt.pix.height;
                return true;
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

            bool format_ok = try_capture_formats(width_, height_);
            if (!format_ok && (width_ != kFallbackWidth || height_ != kFallbackHeight))
            {
                format_ok = try_capture_formats(kFallbackWidth, kFallbackHeight);
            }
            if (!format_ok)
            {
                if (!format_auto)
                {
                    LOGE("Requested USB format not supported: %s\n", desired_format.c_str());
                }
                log_errno("Set format");
                return false;
            }

            if (pixel_format_ == V4L2_PIX_FMT_MJPEG)
            {
                LOGI("USB capture format: MJPEG (decode in modules/decode)\n");
            }

            const double requested_fps = target_fps_ > 0.0 ? target_fps_ : 30.0;
            if (requested_fps > 0.0)
            {
                struct v4l2_streamparm parm = {};
                parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
            actual_parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
            req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
                                      "USB V4L2: %s target fmt=%s size=%dx%d fps=%.2f"
                                      " -> actual fmt=%s size=%dx%d",
                                      device_.c_str(),
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
                offset += snprintf(summary + offset, sizeof(summary) - offset,
                                   " buffers=%d", buffer_count_);
                if (buffer_count_ != req_count)
                {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                                       " (requested %d)", req_count);
                }
                LOGI("%s\n", summary);
            }

            for (int i = 0; i < buffer_count_; ++i)
            {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

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

            return true;
        }

        bool UsbV4L2Camera::start()
        {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0)
            {
                log_errno("Stream on");
                return false;
            }
            streaming_ = true;
            return true;
        }

        bool UsbV4L2Camera::stop()
        {
            if (fd_ < 0 || !streaming_)
                return true;
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
            {
                log_errno("Stream off");
                return false;
            }
            streaming_ = false;
            return true;
        }

        bool UsbV4L2Camera::getFrame(SourceFrame *frame)
        {
            if (!frame)
                return false;

            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

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

            bool ok = true;
            frame->frame.release();
            frame->data.clear();
            frame->width = frame_width_;
            frame->height = frame_height_;
            frame->format = SourceFrameFormat::kUnknown;

            unsigned char *data = static_cast<unsigned char *>(buffers_[buf.index].start);
            if (pixel_format_ == V4L2_PIX_FMT_MJPEG)
            {
                if (buf.bytesused == 0)
                {
                    LOGW("MJPEG frame empty (bytesused=0)\n");
                    ok = false;
                }
                else
                {
                    const size_t used = static_cast<size_t>(buf.bytesused);
                    frame->data.assign(data, data + used);
                    frame->format = SourceFrameFormat::kMjpeg;
                }
            }
            else if (pixel_format_ == V4L2_PIX_FMT_YUYV)
            {
                const size_t expected =
                    static_cast<size_t>(frame_height_) *
                    static_cast<size_t>(frame_width_) * 2;
                const size_t used = buf.bytesused > 0 ? static_cast<size_t>(buf.bytesused) : expected;
                if (used < expected)
                {
                    LOGE("YUYV bytesused too small: %zu < %zu\n", used, expected);
                    ok = false;
                }
                else
                {
                    frame->data.assign(data, data + expected);
                    frame->format = SourceFrameFormat::kYuyv;
                }
            }
            else
            {
                LOGE("Unsupported capture format: %s\n",
                     fourcc_to_string(pixel_format_).c_str());
                ok = false;
            }

            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            {
                log_errno("Requeue buffer");
                return false;
            }

            return ok;
        }

        UsbCamSource::UsbCamSource(std::string device,
                                   int width,
                                   int height,
                                   double fps,
                                   std::string format)
            : device_(std::move(device)),
              width_(width),
              height_(height),
              fps_(fps),
              format_(std::move(format))
        {
        }

        UsbCamSource::~UsbCamSource() = default;

        bool UsbCamSource::Open()
        {
            const int cam_w = width_ > 0 ? width_ : kFallbackWidth;
            const int cam_h = height_ > 0 ? height_ : kFallbackHeight;
            const double cam_fps = fps_ > 0.0 ? fps_ : 30.0;
            const std::string cam_format = format_.empty() ? "auto" : format_;

            camera_ = std::make_unique<UsbV4L2Camera>(
                device_, cam_w, cam_h, kDefaultCameraBufferCount,
                cam_fps, cam_format);
            if (!camera_->init() || !camera_->start())
            {
                camera_.reset();
                return false;
            }
            return true;
        }

        void UsbCamSource::Close()
        {
            camera_.reset();
        }

        bool UsbCamSource::Read(SourceFrame *out)
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
