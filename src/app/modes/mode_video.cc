#include "app_modes.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

#include "app_log.h"
#include "app_utils.h"
#include "frame_pipeline.h"
#include "modes/fps_tracker.h"
#include "worker_pool.h"

bool run_video_mode(const char* input_arg,
                    WorkerPool* pool,
                    RunReport* report,
                    const char* window_name,
                    double max_fps,
                    int resize_width,
                    int resize_height)
{
    if (!pool || pool->workers.empty() || !report) return false;
    if (max_fps < 0.0) max_fps = 0.0;

    const char* window = (window_name && window_name[0] != '\0')
        ? window_name
        : "dock_blindspot";
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        cv::namedWindow(window, cv::WINDOW_NORMAL | cv::WINDOW_FREERATIO);
        cv::setWindowProperty(window, cv::WND_PROP_AUTOSIZE, 0);
    }

    cv::VideoCapture cap(input_arg);
    if (!cap.isOpened()) {
        LOGE("open video failed: %s\n", input_arg);
        std::string msg = "Open video failed\n";
        msg += input_arg ? input_arg : "(null)";
        show_error_window(window, msg);
        return false;
    }

    cv::Mat frame;
    cv::Mat resized;
    FpsTracker fps;
    FramePipeline pipeline(&pool->workers, &pool->results);

    double last_infer_ms = 0.0;
    int total_frames = 0;
    int total_detections = 0;
    double total_infer_ms = 0.0;
    int report_w = 0;
    int report_h = 0;
    auto run_t0 = std::chrono::steady_clock::now();
    auto frame_interval = std::chrono::steady_clock::duration::zero();
    if (max_fps > 0.0) {
        frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / max_fps));
    }
    auto next_frame_time = std::chrono::steady_clock::now();
    auto handle_ready = [&](FrameResult& ready) -> bool {
        bool did_infer = true;
        last_infer_ms = ready.infer_ms;
        total_infer_ms += last_infer_ms;
        total_detections += ready.detections;
        total_frames++;

        double fps_display = fps.on_frame(did_infer);

        char info[128];
        snprintf(info, sizeof(info),
                 "FPS: %.1f | Infer: %.1f ms",
                 fps_display, last_infer_ms);

        cv::putText(ready.frame, info,
            cv::Point(10, 30),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8, cv::Scalar(0, 255, 0), 2);

        bool exit_pressed = false;
        {
            std::lock_guard<std::mutex> lk(g_ui_mutex);
            cv::imshow(window, ready.frame);
            exit_pressed = (cv::waitKey(1) == 27);
        }
        if (exit_pressed) {
            g_stop = 1;
        }

        return exit_pressed;
    };

    while (!g_stop) {
        if (max_fps > 0.0) {
            auto now = std::chrono::steady_clock::now();
            if (now < next_frame_time) {
                std::this_thread::sleep_until(next_frame_time);
            }
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
        }
        auto frame_t0 = std::chrono::steady_clock::now();
        if (!cap.read(frame)) break;
        const cv::Mat* use_frame = &frame;
        if (resize_width > 0 && resize_height > 0 &&
            (frame.cols != resize_width || frame.rows != resize_height)) {
            cv::resize(frame, resized, cv::Size(resize_width, resize_height));
            use_frame = &resized;
        }
        if (report_w == 0 && report_h == 0) {
            report_w = use_frame->cols;
            report_h = use_frame->rows;
        }
        pipeline.WaitForCapacity();
        pipeline.EnqueueFrame(*use_frame, frame_t0);

        FrameResult ready;
        if (pipeline.PopReady(&ready)) {
            if (handle_ready(ready)) break;
        }
    }

    while (pipeline.in_flight() > 0) {
        FrameResult ready;
        if (pipeline.WaitNextReady(&ready)) {
            if (handle_ready(ready)) break;
        }
    }
    FrameResult ready;
    while (pipeline.PopReady(&ready)) {
        if (handle_ready(ready)) break;
    }

    auto run_t1 = std::chrono::steady_clock::now();
    report->total_frames = total_frames;
    report->total_detections = total_detections;
    report->total_infer_ms = total_infer_ms;
    report->total_pipeline_ms = 0.0;
    report->total_elapsed_s =
        std::chrono::duration<double>(run_t1 - run_t0).count();
    report->input_width = report_w;
    report->input_height = report_h;
    report->output_path = "No out";
    report->ok = true;
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        cv::destroyWindow(window);
    }
    return true;
}
