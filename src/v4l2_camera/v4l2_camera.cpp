#include "v4l2_camera.h"
#include "app_log.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <turbojpeg.h>

namespace {
constexpr int kNv12PlaneCount = 2;
constexpr int kFallbackWidth = 640;
constexpr int kFallbackHeight = 480;

void log_errno(const char* action)
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
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string sanitize_filename(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '.' || ch == '-' || ch == '_') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

bool ensure_dir_recursive(const std::string& path)
{
    if (path.empty()) return false;
    std::string current;
    size_t start = 0;
    if (path[0] == '/') {
        current = "/";
        start = 1;
    }
    size_t end = start;
    while (end <= path.size()) {
        if (end == path.size() || path[end] == '/') {
            if (end > start) {
                std::string part = path.substr(start, end - start);
                if (!current.empty() && current.back() != '/') {
                    current.push_back('/');
                }
                current += part;
                struct stat st = {};
                if (stat(current.c_str(), &st) < 0) {
                    if (mkdir(current.c_str(), 0755) < 0 && errno != EEXIST) {
                        log_errno("Create dump directory");
                        return false;
                    }
                } else if (!S_ISDIR(st.st_mode)) {
                    LOGE("Dump path is not a directory: %s\n", current.c_str());
                    return false;
                }
            }
            start = end + 1;
        }
        ++end;
    }
    return true;
}
}  // namespace

V4L2Camera::V4L2Camera(const std::string& device,
                       const int width,
                       const int hight,
                       const int buffer_count,
                       const double target_fps,
                       const std::string& format,
                       const bool allow_format_fallback,
                       const std::string& mjpeg_dump_dir,
                       const int mjpeg_dump_max)
    : fd_(-1),
      device_(device),
      width_(width),
      hight_(hight),
      frame_width_(width),
      frame_height_(hight),
      is_mplane_(false),
      pixel_format_(0),
      plane_count_(1),
      jpeg_supported_(true),
      streaming_(false),
      buffer_count_(buffer_count),
      target_fps_(target_fps),
      format_(format),
      allow_format_fallback_(allow_format_fallback),
      actual_fps_(0.0),
      mjpeg_dump_dir_(mjpeg_dump_dir),
      mjpeg_dump_max_(mjpeg_dump_max),
      mjpeg_dump_count_(0),
      tj_handle_(nullptr)
{
}

bool V4L2Camera::dump_mjpeg_frame(const unsigned char* data, size_t length)
{
    if (!data || length == 0) return false;
    if (mjpeg_dump_dir_.empty()) return false;
    if (mjpeg_dump_max_ > 0 && mjpeg_dump_count_ >= mjpeg_dump_max_) {
        return false;
    }
    if (!ensure_dir_recursive(mjpeg_dump_dir_)) return false;

    std::string safe_device = sanitize_filename(device_);
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%s_%03d.mjpg",
             mjpeg_dump_dir_.c_str(),
             safe_device.c_str(),
             mjpeg_dump_count_);

    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_errno("Open dump file");
        return false;
    }
    size_t written = fwrite(data, 1, length, fp);
    fclose(fp);
    if (written != length) {
        LOGW("Dumped MJPEG frame size mismatch: %zu/%zu\n", written, length);
    } else {
        LOGW("Dumped MJPEG frame to %s (%zu bytes)\n", filename, length);
    }
    mjpeg_dump_count_++;
    return true;
}

bool decode_mjpeg_turbo(void** handle,
                        const unsigned char* data,
                        size_t length,
                        cv::Mat& frame)
{
    if (!handle || !data || length == 0) return false;
    if (!*handle) {
        *handle = tjInitDecompress();
        if (!*handle) {
            LOGE("tjInitDecompress failed\n");
            return false;
        }
    }
    int width = 0;
    int height = 0;
    int subsamp = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(*handle, data, static_cast<unsigned long>(length),
                            &width, &height, &subsamp, &colorspace) != 0) {
        LOGE("turbojpeg header decode failed: %s\n",
             tjGetErrorStr2(*handle));
        return false;
    }
    if (width <= 0 || height <= 0) return false;
    frame.create(height, width, CV_8UC3);
    int pitch = width * 3;
    if (tjDecompress2(*handle, data, static_cast<unsigned long>(length),
                      frame.data, width, pitch, height, TJPF_BGR, 0) != 0) {
        LOGE("turbojpeg decode failed: %s\n",
             tjGetErrorStr2(*handle));
        frame.release();
        return false;
    }
    return true;
}

V4L2Camera::~V4L2Camera()
{
    stop();
    if (tj_handle_) {
        tjDestroy(tj_handle_);
        tj_handle_ = nullptr;
    }
    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (buffers_[i].start && buffers_[i].start != MAP_FAILED &&
            buffers_[i].length > 0)
            munmap(buffers_[i].start, buffers_[i].length);
    }
    if (fd_ >= 0)
        close(fd_);
}

bool V4L2Camera::init()
{
    fd_ = open(device_.c_str(), O_RDWR);
    if (fd_ < 0) {
        log_errno("Open device");
        return false;
    }

    struct v4l2_capability cap = {};
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        log_errno("Query capabilities");
        return false;
    }
    uint32_t caps = cap.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS) caps = cap.device_caps;
    bool cap_mplane = (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    bool cap_capture = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
    if (!cap_mplane && !cap_capture) {
        LOGE("Device does not support video capture\n");
        return false;
    }
    if (!(caps & V4L2_CAP_STREAMING)) {
        LOGE("Device does not support streaming\n");
        return false;
    }

    jpeg_supported_ = true;

    std::string desired_format = to_lower(format_);
    if (desired_format == "mjpg") desired_format = "mjpeg";
    if (desired_format.empty()) desired_format = "auto";
    bool format_auto = desired_format == "auto";
    bool want_mjpeg = desired_format == "mjpeg";
    bool want_yuyv = desired_format == "yuyv";
    bool want_nv12 = desired_format == "nv12";
    if (!format_auto && !want_mjpeg && !want_yuyv && !want_nv12) {
        LOGE("Unsupported format requested: %s\n", format_.c_str());
        return false;
    }
    bool allow_fallback = allow_format_fallback_;
    if (want_mjpeg && !jpeg_supported_) {
        if (!allow_fallback) {
            LOGE("MJPEG requested but decoder not available\n");
            return false;
        }
        LOGW("MJPEG requested but decoder not available; fallback enabled\n");
    }

    auto try_set_mplane = [&](uint32_t pixfmt, int width, int height) -> bool {
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = pixfmt;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes = kNv12PlaneCount;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            return false;
        }
        is_mplane_ = true;
        pixel_format_ = fmt.fmt.pix_mp.pixelformat;
        plane_count_ = fmt.fmt.pix_mp.num_planes;
        frame_width_ = fmt.fmt.pix_mp.width;
        frame_height_ = fmt.fmt.pix_mp.height;
        return true;
    };

    auto try_set_capture = [&](uint32_t pixfmt, int width, int height) -> bool {
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            return false;
        }
        is_mplane_ = false;
        pixel_format_ = fmt.fmt.pix.pixelformat;
        plane_count_ = 1;
        frame_width_ = fmt.fmt.pix.width;
        frame_height_ = fmt.fmt.pix.height;
        return true;
    };

    auto push_unique = [](std::vector<uint32_t>* list, uint32_t fmt) {
        if (std::find(list->begin(), list->end(), fmt) == list->end()) {
            list->push_back(fmt);
        }
    };
    auto add_auto_capture = [&](std::vector<uint32_t>* list) {
        push_unique(list, V4L2_PIX_FMT_MJPEG);
        push_unique(list, V4L2_PIX_FMT_YUYV);
        push_unique(list, V4L2_PIX_FMT_NV12);
    };

    std::vector<uint32_t> mplane_formats;
    std::vector<uint32_t> capture_formats;
    if (cap_mplane && (format_auto || want_nv12 || allow_fallback)) {
        push_unique(&mplane_formats, V4L2_PIX_FMT_NV12);
    }
    if (cap_capture) {
        if (format_auto) {
            add_auto_capture(&capture_formats);
        } else if (want_mjpeg) {
            if (jpeg_supported_) {
                push_unique(&capture_formats, V4L2_PIX_FMT_MJPEG);
            }
            if (allow_fallback) add_auto_capture(&capture_formats);
        } else if (want_yuyv) {
            push_unique(&capture_formats, V4L2_PIX_FMT_YUYV);
            if (allow_fallback) add_auto_capture(&capture_formats);
        } else if (want_nv12) {
            push_unique(&capture_formats, V4L2_PIX_FMT_NV12);
            if (allow_fallback) add_auto_capture(&capture_formats);
        }
    }

    auto try_mplane_formats = [&](const std::vector<uint32_t>& formats,
                                  int width, int height) -> bool {
        if (formats.empty()) return false;
        for (uint32_t fmt : formats) {
            if (try_set_mplane(fmt, width, height)) return true;
        }
        return false;
    };
    auto try_capture_formats = [&](const std::vector<uint32_t>& formats,
                                   int width, int height) -> bool {
        if (formats.empty()) return false;
        for (uint32_t fmt : formats) {
            if (fmt == V4L2_PIX_FMT_MJPEG && !jpeg_supported_) {
                continue;
            }
            if (try_set_capture(fmt, width, height)) return true;
        }
        return false;
    };

    bool prefer_mplane = format_auto || want_nv12;
    auto try_with_size = [&](int width, int height) -> bool {
        bool ok = false;
        if (prefer_mplane) {
            if (cap_mplane) {
                ok = try_mplane_formats(mplane_formats, width, height);
            }
            if (!ok && cap_capture) {
                ok = try_capture_formats(capture_formats, width, height);
            }
        } else {
            if (cap_capture) {
                ok = try_capture_formats(capture_formats, width, height);
            }
            if (!ok && cap_mplane) {
                ok = try_mplane_formats(mplane_formats, width, height);
            }
        }
        return ok;
    };

    bool format_ok = try_with_size(width_, hight_);
    if (!format_ok &&
        (width_ != kFallbackWidth || hight_ != kFallbackHeight)) {
        format_ok = try_with_size(kFallbackWidth, kFallbackHeight);
    }

    if (!format_ok) {
        if (!format_auto) {
            LOGE("Requested format not supported: %s\n",
                 desired_format.c_str());
        }
        log_errno("Set format");
        return false;
    }
    if (is_mplane_ && (plane_count_ < 1 || plane_count_ > kNv12PlaneCount)) {
        LOGE("Unsupported plane count: %d\n", plane_count_);
        return false;
    }
    if (!is_mplane_ && pixel_format_ == V4L2_PIX_FMT_MJPEG) {
        LOGI("MJPEG decoder: turbojpeg\n");
    }

    double requested_fps = target_fps_ > 0.0 ? target_fps_ : 30.0;
    if (requested_fps > 0.0) {
        struct v4l2_streamparm parm = {};
        parm.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                               : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        double rounded = std::round(requested_fps);
        if (std::fabs(requested_fps - rounded) < 1e-3) {
            parm.parm.capture.timeperframe.numerator = 1;
            parm.parm.capture.timeperframe.denominator =
                static_cast<unsigned int>(rounded);
        } else {
            const double base = 1000.0;
            double num = base / requested_fps;
            if (num < 1.0) num = 1.0;
            parm.parm.capture.timeperframe.numerator =
                static_cast<unsigned int>(std::lround(num));
            parm.parm.capture.timeperframe.denominator =
                static_cast<unsigned int>(base);
        }
        if (ioctl(fd_, VIDIOC_S_PARM, &parm) < 0) {
            log_errno("Set frame rate");
        }
    }

    struct v4l2_streamparm actual_parm = {};
    actual_parm.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                  : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_G_PARM, &actual_parm) < 0) {
        log_errno("Get frame rate");
    } else if (actual_parm.parm.capture.timeperframe.numerator > 0) {
        actual_fps_ = static_cast<double>(
            actual_parm.parm.capture.timeperframe.denominator) /
            static_cast<double>(
                actual_parm.parm.capture.timeperframe.numerator);
    }

    struct v4l2_requestbuffers req = {};
    int req_count = buffer_count_;
    if (req_count <= 0) {
        req_count = DEFAULT_BUFFER_COUNT;
    }
    if (req_count > MAX_BUFFER_COUNT) {
        LOGW("Requested buffer count %d too large, clamp to %d\n",
             req_count, MAX_BUFFER_COUNT);
        req_count = MAX_BUFFER_COUNT;
    }
    req.count = req_count;
    req.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                          : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        log_errno("Request buffers");
        return false;
    }
    if (req.count < 1) {
        LOGE("No buffers allocated for %s\n", device_.c_str());
        return false;
    }
    buffer_count_ = static_cast<int>(req.count);
    buffers_.assign(buffer_count_, {});

    {
        char summary[384];
        int offset = snprintf(summary, sizeof(summary),
                              "V4L2: %s %s target fmt=%s size=%dx%d fps=%.2f"
                              " -> actual fmt=%s size=%dx%d",
                              device_.c_str(),
                              is_mplane_ ? "mplane" : "capture",
                              desired_format.c_str(),
                              width_, hight_,
                              requested_fps,
                              fourcc_to_string(pixel_format_).c_str(),
                              frame_width_, frame_height_);
        if (actual_fps_ > 0.0) {
            offset += snprintf(summary + offset, sizeof(summary) - offset,
                               " fps=%.2f", actual_fps_);
        } else {
            offset += snprintf(summary + offset, sizeof(summary) - offset,
                               " fps=unknown");
        }
        if (is_mplane_) {
            offset += snprintf(summary + offset, sizeof(summary) - offset,
                               " planes=%d", plane_count_);
        }
        offset += snprintf(summary + offset, sizeof(summary) - offset,
                           " buffers=%d", buffer_count_);
        if (buffer_count_ != req_count) {
            offset += snprintf(summary + offset, sizeof(summary) - offset,
                               " (requested %d)", req_count);
        }
        LOGI("%s\n", summary);
    }

    for (int i = 0; i < buffer_count_; ++i) {
        struct v4l2_buffer buf = {};
        buf.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                              : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (is_mplane_) {
            struct v4l2_plane planes[kNv12PlaneCount] = {};
            buf.length = plane_count_;
            buf.m.planes = planes;
            if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                log_errno("Query buffer");
                return false;
            }
            size_t total_len = 0;
            for (int p = 0; p < plane_count_; ++p) {
                total_len += buf.m.planes[p].length;
            }
            buffers_[i].length = total_len;
            buffers_[i].start = mmap(NULL, total_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                                     fd_, buf.m.planes[0].m.mem_offset);
            if (buffers_[i].start == MAP_FAILED) {
                log_errno("mmap");
                return false;
            }
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
                log_errno("Initial queue buffer");
                return false;
            }
        } else {
            if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                log_errno("Query buffer");
                return false;
            }
            buffers_[i].length = buf.length;
            buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                     fd_, buf.m.offset);
            if (buffers_[i].start == MAP_FAILED) {
                log_errno("mmap");
                return false;
            }
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
                log_errno("Initial queue buffer");
                return false;
            }
        }
    }

    return true;
}

bool V4L2Camera::start()
{
    enum v4l2_buf_type type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                         : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        log_errno("Stream on");
        return false;
    }
    streaming_ = true;
    return true;
}

bool V4L2Camera::stop()
{
    if (fd_ < 0 || !streaming_) return true;
    enum v4l2_buf_type type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                         : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
        log_errno("Stream off");
        return false;
    }
    streaming_ = false;
    return true;
}

bool V4L2Camera::getFrame(cv::Mat& frame)
{
    struct v4l2_buffer buf = {};
    buf.type = is_mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                          : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    bool ok = true;
    if (is_mplane_) {
        struct v4l2_plane planes[kNv12PlaneCount] = {};
        buf.length = plane_count_;
        buf.m.planes = planes;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            log_errno("Dequeue buffer");
            return false;
        }
        if (buf.index >= buffers_.size()) {
            LOGE("Invalid buffer index: %u\n", buf.index);
            return false;
        }

        if (pixel_format_ == V4L2_PIX_FMT_NV12) {
            unsigned char* y_plane =
                static_cast<unsigned char*>(buffers_[buf.index].start);
            cv::Mat nv12(frame_height_ * 3 / 2, frame_width_, CV_8UC1, y_plane);
            cv::cvtColor(nv12, frame, cv::COLOR_YUV2BGR_NV12);
        } else {
            LOGE("Unsupported mplane format: %s\n",
                 fourcc_to_string(pixel_format_).c_str());
            ok = false;
        }
    } else {
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            log_errno("Dequeue buffer");
            return false;
        }
        if (buf.index >= buffers_.size()) {
            LOGE("Invalid buffer index: %u\n", buf.index);
            return false;
        }

        unsigned char* data =
            static_cast<unsigned char*>(buffers_[buf.index].start);
        if (pixel_format_ == V4L2_PIX_FMT_MJPEG) {
            if (buf.bytesused == 0) {
                LOGW("MJPEG frame empty (bytesused=0)\n");
                ok = false;
            } else {
                if (!decode_mjpeg_turbo(&tj_handle_,
                                        data,
                                        static_cast<size_t>(buf.bytesused),
                                        frame)) {
                    LOGE("MJPEG decode failed; bytesused=%u\n", buf.bytesused);
                    dump_mjpeg_frame(data,
                                     static_cast<size_t>(buf.bytesused));
                    ok = false;
                }
            }
        } else if (pixel_format_ == V4L2_PIX_FMT_YUYV) {
            cv::Mat yuyv(frame_height_, frame_width_, CV_8UC2, data);
            cv::cvtColor(yuyv, frame, cv::COLOR_YUV2BGR_YUYV);
        } else if (pixel_format_ == V4L2_PIX_FMT_NV12) {
            cv::Mat nv12(frame_height_ * 3 / 2, frame_width_, CV_8UC1, data);
            cv::cvtColor(nv12, frame, cv::COLOR_YUV2BGR_NV12);
        } else {
            LOGE("Unsupported capture format: %s\n",
                 fourcc_to_string(pixel_format_).c_str());
            ok = false;
        }
    }

    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        log_errno("Requeue buffer");
        return false;
    }

    return ok;
}
