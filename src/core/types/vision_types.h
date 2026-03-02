#ifndef CORE_TYPES_VISION_TYPES_H_
#define CORE_TYPES_VISION_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMAGE_FORMAT_GRAY8,
    IMAGE_FORMAT_RGB888,
    IMAGE_FORMAT_RGBA8888,
    IMAGE_FORMAT_YUV420SP_NV21,
    IMAGE_FORMAT_YUV420SP_NV12,
    IMAGE_FORMAT_BGR888,
} image_format_t;

typedef struct {
    int width;
    int height;
    int width_stride;
    int height_stride;
    image_format_t format;
    unsigned char* virt_addr;
    int size;
    int fd;
} image_buffer_t;

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} image_rect_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    float angle;
} image_obb_box_t;

typedef struct {
    int x_pad;
    int y_pad;
    float scale;
} letterbox_t;

#ifdef __cplusplus
} // extern "C"

#include <chrono>
#include <vector>

#include <opencv2/opencv.hpp>

namespace core
{
    namespace types
    {

        enum class SourceFrameFormat
        {
            kUnknown = 0,
            kBgr,
            kMjpeg,
            kNv12,
            kYuyv,
        };

        inline const char *ToString(SourceFrameFormat fmt)
        {
            switch (fmt)
            {
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

        struct SourceFrame
        {
            SourceFrameFormat format = SourceFrameFormat::kUnknown;
            int width = 0;
            int height = 0;
            cv::Mat frame;
            std::vector<unsigned char> data;
            std::chrono::steady_clock::time_point capture_tp;
        };

    } // namespace types
} // namespace core
#endif // __cplusplus

#endif // CORE_TYPES_VISION_TYPES_H_
