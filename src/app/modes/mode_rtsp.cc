#include "app_modes.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "app_log.h"
#include "app_utils.h"
#include "frame_pipeline.h"
#include "modes/fps_tracker.h"
#include "worker_pool.h"

namespace {
constexpr int kRtspLatencyMs = 200;
constexpr int kReconnectMaxAttempts = 10;
const auto kReconnectBaseDelay = std::chrono::milliseconds(200);
const auto kReconnectMaxDelay = std::chrono::milliseconds(5000);

std::string to_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string normalize_codec(const std::string& codec)
{
    if (codec.empty()) return "auto";
    std::string lower = to_lower(codec);
    if (lower == "mjpg") lower = "mjpeg";
    if (lower == "h264" || lower == "h265" || lower == "auto") {
        return lower;
    }
    return "auto";
}

std::string escape_gst_string(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string build_rtsp_mpp_pipeline(const std::string& url,
                                    const std::string& codec)
{
    const std::string depay = (codec == "h265") ? "rtph265depay" : "rtph264depay";
    const std::string parser = (codec == "h265") ? "h265parse" : "h264parse";
    const std::string escaped_url = escape_gst_string(url);

    char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "rtspsrc location=\"%s\" protocols=tcp latency=%d ! "
             "%s ! %s ! mppvideodec ! videoconvert ! "
             "video/x-raw,format=BGR ! appsink sync=false drop=true max-buffers=1",
             escaped_url.c_str(), kRtspLatencyMs,
             depay.c_str(), parser.c_str());
    return std::string(pipeline);
}

std::string build_rtsp_decodebin_pipeline(const std::string& url)
{
    const std::string escaped_url = escape_gst_string(url);
    char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "uridecodebin uri=\"%s\" ! videoconvert ! "
             "video/x-raw,format=BGR ! appsink sync=false drop=true max-buffers=1",
             escaped_url.c_str());
    return std::string(pipeline);
}

bool try_open_capture(cv::VideoCapture* cap,
                      const std::string& source,
                      int backend,
                      const char* desc)
{
    cap->release();
    if (!cap->open(source, backend) || !cap->isOpened()) {
        return false;
    }
    LOGI("RTSP opened via %s\n", desc);
    return true;
}

bool open_rtsp_capture(const std::string& url,
                       const std::string& codec,
                       cv::VideoCapture* cap)
{
    if (!cap) return false;
    const std::string codec_mode = normalize_codec(codec);
    std::vector<std::pair<std::string, std::string>> gst_attempts;

    if (codec_mode == "h264") {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h264"),
                                  "gstreamer rtsp h264 mpp");
    } else if (codec_mode == "h265") {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h265"),
                                  "gstreamer rtsp h265 mpp");
    } else {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h264"),
                                  "gstreamer rtsp h264 mpp");
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h265"),
                                  "gstreamer rtsp h265 mpp");
    }
    gst_attempts.emplace_back(build_rtsp_decodebin_pipeline(url),
                              "gstreamer uridecodebin");

    for (const auto& item : gst_attempts) {
        if (try_open_capture(cap, item.first, cv::CAP_GSTREAMER, item.second.c_str())) {
            return true;
        }
    }

    if (try_open_capture(cap, url, cv::CAP_GSTREAMER, "gstreamer direct rtsp")) {
        return true;
    }
    if (try_open_capture(cap, url, cv::CAP_ANY, "opencv default backend")) {
        return true;
    }

    cap->release();
    return false;
}
}  // namespace

bool run_rtsp_mode(const char* input_arg,
                   WorkerPool* pool,
                   RunReport* report,
                   const char* window_name,
                   double max_fps,
                   int resize_width,
                   int resize_height,
                   const std::string& codec)
{
    if (!pool || pool->workers.empty() || !report || !input_arg || input_arg[0] == '\0') {
        return false;
    }
    if (max_fps < 0.0) max_fps = 0.0;

    const char* window = (window_name && window_name[0] != '\0')
        ? window_name
        : "dock_blindspot";
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        cv::namedWindow(window, cv::WINDOW_NORMAL | cv::WINDOW_FREERATIO);
        cv::setWindowProperty(window, cv::WND_PROP_AUTOSIZE, 0);
    }

    const std::string rtsp_url = input_arg;
    cv::VideoCapture cap;

    auto open_stream = [&]() -> bool {
        return open_rtsp_capture(rtsp_url, codec, &cap);
    };
    auto reconnect_stream = [&](const char* reason) -> bool {
        std::string msg = "RTSP error, reconnecting\n";
        msg += rtsp_url;
        msg += "\n";
        msg += reason;
        show_error_window(window, msg);
        LOGW("rtsp error (%s), reconnecting: %s\n", reason, rtsp_url.c_str());

        auto delay = kReconnectBaseDelay;
        for (int attempt = 1; attempt <= kReconnectMaxAttempts && !g_stop; ++attempt) {
            if (attempt > 1) {
                std::this_thread::sleep_for(delay);
                if (delay < kReconnectMaxDelay) {
                    auto next = delay * 2;
                    delay = next < kReconnectMaxDelay ? next : kReconnectMaxDelay;
                }
            }
            if (open_stream()) {
                LOGI("rtsp reconnected: %s\n", rtsp_url.c_str());
                return true;
            }
            LOGW("rtsp reconnect attempt %d/%d failed: %s\n",
                 attempt, kReconnectMaxAttempts, rtsp_url.c_str());
        }
        return false;
    };

    if (!open_stream()) {
        LOGE("open rtsp failed: %s\n", rtsp_url.c_str());
        if (!reconnect_stream("open failed")) {
            std::string msg = "RTSP reconnect failed\n";
            msg += rtsp_url;
            show_error_window(window, msg);
            return false;
        }
    }

    cv::Mat frame;
    cv::Mat resized;
    FpsTracker fps;
    FramePipeline pipeline(&pool->workers, &pool->results);

    bool got_frame = false;
    bool rtsp_ok = true;
    bool keep_error_window = false;
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
        last_infer_ms = ready.infer_ms;
        total_infer_ms += last_infer_ms;
        total_detections += ready.detections;
        total_frames++;

        const double fps_display = fps.on_frame(true);
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
        if (!cap.read(frame) || frame.empty()) {
            LOGE("rtsp frame capture failed: %s\n", rtsp_url.c_str());
            if (!reconnect_stream("capture failed")) {
                rtsp_ok = false;
                std::string msg = "RTSP reconnect failed\n";
                msg += rtsp_url;
                show_error_window(window, msg);
                keep_error_window = true;
                break;
            }
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
            continue;
        }

        got_frame = true;
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
    report->type = INPUT_RTSP;
    report->total_frames = total_frames;
    report->total_detections = total_detections;
    report->total_infer_ms = total_infer_ms;
    report->total_pipeline_ms = 0.0;
    report->total_elapsed_s =
        std::chrono::duration<double>(run_t1 - run_t0).count();
    report->input_width = report_w;
    report->input_height = report_h;
    report->output_path = "No out";
    report->ok = rtsp_ok && (got_frame || g_stop);
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        if (!keep_error_window) {
            cv::destroyWindow(window);
        }
    }
    return rtsp_ok;
}
