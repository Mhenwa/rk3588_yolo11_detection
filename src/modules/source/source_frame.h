#ifndef MODULES_SOURCE_SOURCE_FRAME_H_
#define MODULES_SOURCE_SOURCE_FRAME_H_

#include <chrono>
#include <vector>

#include <opencv2/opencv.hpp>

namespace modules {
namespace source {

enum class SourceFrameFormat {
    kUnknown = 0,
    kBgr,
    kMjpeg,
    kNv12,
    kYuyv,
};

inline const char* ToString(SourceFrameFormat fmt)
{
    switch (fmt) {
    case SourceFrameFormat::kBgr:
        return "bgr";
    case SourceFrameFormat::kMjpeg:
        return "mjpeg";
    case SourceFrameFormat::kNv12:
        return "nv12";
    case SourceFrameFormat::kYuyv:
        return "yuyv";
    default:
        return "unknown";
    }
}

struct SourceFrame {
    SourceFrameFormat format = SourceFrameFormat::kUnknown;
    int width = 0;
    int height = 0;
    cv::Mat frame;
    std::vector<unsigned char> data;
    std::chrono::steady_clock::time_point capture_tp;
};

}  // namespace source
}  // namespace modules

#endif  // MODULES_SOURCE_SOURCE_FRAME_H_
