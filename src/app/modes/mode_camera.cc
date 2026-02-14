#include "app_modes.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

#include "app_log.h"
#include "app_utils.h"
#include "frame_pipeline.h"
#include "modes/fps_tracker.h"
#include "v4l2_camera.h"
#include "worker_pool.h"

bool run_camera_mode(const char* input_arg,
                     WorkerPool* pool,
                     RunReport* report,
                     const char* window_name,
                     double max_fps,
                     int width,
                     int height,
                     int buffers,
                     double fps,
                     const std::string& format)
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

    std::string device;
    if (is_number_str(input_arg)) {
        int index = atoi(input_arg);
        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/video%d", index);
        device = dev_path;
    } else {
        device = input_arg;
    }

    int cam_w = width > 0 ? width : 640;
    int cam_h = height > 0 ? height : 640;
    int cam_buffers = buffers > 0 ? buffers : V4L2Camera::DEFAULT_BUFFER_COUNT;
    double cam_fps = fps > 0.0 ? fps : 30.0;
    std::string cam_format = format.empty() ? "auto" : format;
    report->input_width = cam_w;
    report->input_height = cam_h;
    bool display_frames = true;
    constexpr int kMaxDisplayWidth = 1280;  // 最大窗口尺寸
    constexpr int kMaxDisplayHeight = 720;
    bool window_sized = false;
    int display_w = 0;
    int display_h = 0;
    constexpr int kReconnectMaxAttempts = 10;
    const auto kReconnectBaseDelay = std::chrono::milliseconds(200);
    const auto kReconnectMaxDelay = std::chrono::milliseconds(5000);

    std::unique_ptr<V4L2Camera> cam;
    auto open_camera = [&]() -> bool {
        cam = std::make_unique<V4L2Camera>(device, cam_w, cam_h, cam_buffers,
                                           cam_fps, cam_format);
        if (!cam->init() || !cam->start()) {
            cam.reset();
            return false;
        }
        return true;
    };
    auto reconnect_camera = [&](const char* reason) -> bool {
        std::string msg = "Camera error, reconnecting\n";
        msg += device;
        msg += "\n";
        msg += reason;
        show_error_window(window, msg);
        LOGW("camera error (%s), reconnecting: %s\n", reason, device.c_str());
        auto delay = kReconnectBaseDelay;
        for (int attempt = 1; attempt <= kReconnectMaxAttempts && !g_stop; ++attempt) {
            if (attempt > 1) {
                std::this_thread::sleep_for(delay);
                if (delay < kReconnectMaxDelay) {
                    auto next = delay * 2;
                    delay = next < kReconnectMaxDelay ? next : kReconnectMaxDelay;
                }
            }
            if (open_camera()) {
                LOGI("camera reconnected: %s\n", device.c_str());
                return true;
            }
            LOGW("camera reconnect attempt %d/%d failed: %s\n",
                 attempt, kReconnectMaxAttempts, device.c_str());
        }
        return false;
    };

    if (!open_camera()) {
        LOGE("open camera failed: %s\n", device.c_str());
        display_frames = false;
        if (!reconnect_camera("open failed")) {
            std::string msg = "Camera reconnect failed\n";
            msg += device;
            show_error_window(window, msg);
            return false;
        }
        display_frames = true;
    }

    cv::Mat frame;
    FpsTracker fps_tracker;
    FramePipeline pipeline(&pool->workers, &pool->results);
    bool got_frame = false;
    bool camera_ok = true;
    bool keep_error_window = false;

    double last_infer_ms = 0.0;
    int total_frames = 0;
    int total_detections = 0;
    double total_infer_ms = 0.0;
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

        double fps_display = fps_tracker.on_frame(did_infer);

        if (!display_frames) {
            return false;
        }

        if (!window_sized && ready.frame.cols > 0 && ready.frame.rows > 0) {
            int frame_w = ready.frame.cols;
            int frame_h = ready.frame.rows;
            float scale = 1.0f;
            if (frame_w > kMaxDisplayWidth || frame_h > kMaxDisplayHeight) {
                float scale_w = static_cast<float>(kMaxDisplayWidth) / frame_w;
                float scale_h = static_cast<float>(kMaxDisplayHeight) / frame_h;
                scale = scale_w < scale_h ? scale_w : scale_h;
            }
            display_w = static_cast<int>(frame_w * scale);
            display_h = static_cast<int>(frame_h * scale);
            if (display_w < 1) display_w = 1;
            if (display_h < 1) display_h = 1;
        }

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
            if (!window_sized && display_w > 0 && display_h > 0) {
                cv::resizeWindow(window, display_w, display_h);
                window_sized = true;
            }
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
        if (!cam || !cam->getFrame(frame)) {
            LOGE("camera frame capture failed: %s\n", device.c_str());
            display_frames = false;
            if (!reconnect_camera("capture failed")) {
                camera_ok = false;
                std::string msg = "Camera reconnect failed\n";
                msg += device;
                show_error_window(window, msg);
                keep_error_window = true;
                break;
            }
            display_frames = true;
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
            continue;
        }
        if (frame.empty()) {
            LOGE("camera returned empty frame: %s\n", device.c_str());
            display_frames = false;
            if (!reconnect_camera("empty frame")) {
                camera_ok = false;
                std::string msg = "Camera reconnect failed\n";
                msg += device;
                show_error_window(window, msg);
                keep_error_window = true;
                break;
            }
            display_frames = true;
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
            continue;
        }
        got_frame = true;
        pipeline.WaitForCapacity();
        pipeline.EnqueueFrame(frame, frame_t0);

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
    report->ok = camera_ok && (got_frame || g_stop);
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        if (!keep_error_window) {
            cv::destroyWindow(window);
        }
    }
    return camera_ok;
}
