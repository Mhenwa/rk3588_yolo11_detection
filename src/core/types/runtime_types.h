#ifndef CORE_TYPES_RUNTIME_TYPES_H_
#define CORE_TYPES_RUNTIME_TYPES_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "core/types/infer_types.h"

enum InputType {
    INPUT_IMAGE, // deprecated
    INPUT_VIDEO,
    INPUT_USB_CAMERA,
    INPUT_MIPI_CAMERA,
    INPUT_RTSP,
    INPUT_VIDEO_CAMERA // deprecated
};

struct RunReport {
    InputType type;
    int total_frames;
    int total_detections;
    int threads_use;
    double total_infer_ms;
    double total_pipeline_ms;
    double total_elapsed_s;
    int input_width;
    int input_height;
    const char* output_path;
    bool ok;
};

struct SourceRunReport {
    std::string name;
    RunReport report;
    int config_width;
    int config_height;
};

namespace core
{
    namespace types
    {

        struct SourceOptions
        {
            InputType type = INPUT_VIDEO;
            std::string input;
            int width = 0;
            int height = 0;
            double fps = 30.0;
            std::string format = "auto";
        };

    } // namespace types
} // namespace core

struct FrameTask
{
    int seq;
    cv::Mat frame;
    bool do_infer;
    std::chrono::steady_clock::time_point start_tp;
};

struct FrameResult
{
    int seq;
    cv::Mat frame;
    std::chrono::steady_clock::time_point start_tp;
    double infer_ms;
    int detections;
};

struct ResultQueue
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<FrameResult> items;
};

struct Worker
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<FrameTask> tasks;
    std::thread th;
    rknn_app_context_t ctx;
    rknn_core_mask core_mask;
    float conf_threshold;
    bool stop;
};

struct FpsTracker {
    int counter = 0;
    double fps_calc = 0.0;
    double display_fps = 0.0;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    double on_frame(bool is_infer_frame)
    {
        counter++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed >= 1.0) {
            fps_calc = counter / elapsed;
            counter = 0;
            t0 = now;
        }
        if (is_infer_frame) {
            display_fps = fps_calc;
        }
        return display_fps;
    }
};

#endif // CORE_TYPES_RUNTIME_TYPES_H_
