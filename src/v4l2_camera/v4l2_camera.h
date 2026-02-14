#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <string>
#include <vector>

class V4L2Camera
{
public:
    static const int DEFAULT_BUFFER_COUNT = 4;
    static const int MAX_BUFFER_COUNT = 8;

    V4L2Camera(const std::string& device,
               const int width,
               const int hight,
               const int buffer_count = DEFAULT_BUFFER_COUNT,
               const double target_fps = 30.0,
               const std::string& format = "auto",
               const bool allow_format_fallback = true,
               const std::string& mjpeg_dump_dir = "logs/mjpeg",
               const int mjpeg_dump_max = 3);
    ~V4L2Camera();

    bool init();
    bool start();
    bool stop();
    bool getFrame(cv::Mat& frame);

private:
    bool dump_mjpeg_frame(const unsigned char* data, size_t length);

    struct V4L2Buffer {
        void* start;
        size_t length;
    };

    int buffer_count_;
    int fd_;
    std::string device_;
    const int width_;
    const int hight_;
    int frame_width_;
    int frame_height_;
    bool is_mplane_;
    uint32_t pixel_format_;
    int plane_count_;
    bool jpeg_supported_;
    bool streaming_;
    std::vector<V4L2Buffer> buffers_;
    double target_fps_;
    std::string format_;
    bool allow_format_fallback_;
    double actual_fps_;
    std::string mjpeg_dump_dir_;
    int mjpeg_dump_max_;
    int mjpeg_dump_count_;
    void* tj_handle_;
};
