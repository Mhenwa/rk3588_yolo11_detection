#include "app_utils.h"

#include "app_log.h"

#include <cctype>
#include <cstdio>
#include <sstream>

#include <opencv2/opencv.hpp>

volatile sig_atomic_t g_stop = 0;
std::mutex g_ui_mutex;

void handle_sigint(int)
{
    g_stop = 1;
}

void print_usage(const char* prog)
{
    LOGI("Usage:\n");
    LOGI("  %s [config.json]\n", prog);
    LOGI("  %s --config config.json\n", prog);
    LOGI("Config keys (JSON):\n");
    LOGI("  general: { mode, model_path, label }  mode=image|video_camera\n");
    LOGI("  modes.image: { input }\n");
    LOGI("  modes.video_camera.sources:\n");
    LOGI("    [ { name, type, input, threads, width, height, buffers, fps, format, conf_threshold } ]\n");
    LOGI("    type=video|camera|rtsp, rtsp format=auto|h264|h265\n");
}

static std::string format_resolution(int width, int height)
{
    if (width > 0 && height > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d x %d", width, height);
        return std::string(buf);
    }
    return "? x ?";
}

static std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    if (lines.empty()) {
        lines.push_back(text);
    }
    return lines;
}

void show_error_window(const char* window_name, const std::string& message)
{
    const char* window = (window_name && window_name[0] != '\0')
        ? window_name
        : "dock_blindspot";
    std::vector<std::string> lines = split_lines(message);
    if (lines.empty()) {
        lines.push_back("Unknown error");
    }

    const int width = 640;
    const int height = 360;
    cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::putText(canvas, "ERROR",
                cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0, cv::Scalar(0, 0, 255), 2);
    int y = 90;
    for (const auto& line : lines) {
        cv::putText(canvas, line,
                    cv::Point(20, y),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.6, cv::Scalar(0, 0, 255), 1);
        y += 30;
        if (y > height - 20) break;
    }

    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        cv::namedWindow(window, cv::WINDOW_NORMAL | cv::WINDOW_FREERATIO);
        cv::setWindowProperty(window, cv::WND_PROP_AUTOSIZE, 0);
        cv::imshow(window, canvas);
        int key = cv::waitKey(1);
        if (key == 27) {
            g_stop = 1;
        }
    }
}

void print_run_report(const RunReport& report,
                      const std::vector<SourceRunReport>* sources)
{
    if (report.type == INPUT_VIDEO_CAMERA && sources && !sources->empty()) {
        puts("");
        LOGI("===== Run Report =====\n");
        LOGI("Mode: video_camera\n");
        for (const auto& src : *sources) {
            const RunReport& r = src.report;
            double avg_infer_ms = 0.0;
            if (r.total_frames > 0) {
                avg_infer_ms = r.total_infer_ms / r.total_frames;
            }
            double avg_fps = 0.0;
            if (r.total_elapsed_s > 0.0) {
                avg_fps = static_cast<double>(r.total_frames) / r.total_elapsed_s;
            }
            int input_w = r.input_width > 0 ? r.input_width : src.config_width;
            int input_h = r.input_height > 0 ? r.input_height : src.config_height;
            std::string resolution = format_resolution(input_w, input_h);

            LOGI("===\n");
            LOGI("Name: %s\n", src.name.c_str());
            LOGI("Status: %s\n", r.ok ? "ok" : "failed");
            LOGI("Avg FPS: %.2f\n", avg_fps);
            
            LOGD("===\n");
            LOGD("Name: %s\n", src.name.c_str());
            LOGD("Status: %s\n", r.ok ? "ok" : "failed");
            LOGD("Threads: %d\n", r.threads_use);
            LOGD("Frames: %d\n", r.total_frames);
            LOGD("Avg infer time: %.2f ms\n", avg_infer_ms);
            LOGD("Total elapsed: %.2f s\n", r.total_elapsed_s);
            LOGD("Avg FPS: %.2f\n", avg_fps);
            LOGD("Input resolution: %s\n", resolution.c_str());
            LOGD("\n");
        }
        LOGI("======================\n");
        return;
    }

    const char* mode = "unknown";
    if (report.type == INPUT_IMAGE) mode = "image";
    else if (report.type == INPUT_VIDEO) mode = "video";
    else if (report.type == INPUT_CAMERA) mode = "camera";
    else if (report.type == INPUT_RTSP) mode = "rtsp";
    else if (report.type == INPUT_VIDEO_CAMERA) mode = "video_camera";
    puts("");
    LOGI("===== Run Report =====\n");
    LOGI("Mode: %s\n", mode);
    LOGI("Status: %s\n", report.ok ? "ok" : "failed");
    LOGI("Threads: %d\n", report.threads_use);
    if (report.total_frames > 0) {
        LOGI("Frames: %d\n", report.total_frames);
        LOGI("Total detections: %d\n", report.total_detections);
        LOGI("Total infer time: %.2f ms\n", report.total_infer_ms);
        LOGI("Avg infer time: %.2f ms\n",
             report.total_infer_ms / report.total_frames);
    }
    if (report.total_elapsed_s > 0.0) {
        LOGI("Total elapsed: %.2f s\n", report.total_elapsed_s);
        if (report.total_frames > 0) {
            LOGI("Avg FPS: %.2f\n",
                 report.total_frames / report.total_elapsed_s);
        }
    }
    if (report.output_path && report.output_path[0] != '\0') {
        LOGI("Output: %s\n", report.output_path);
    }
    LOGI("======================\n");
}

bool is_number_str(const char* s)
{
    if (!s || *s == '\0') return false;
    for (const char* p = s; *p; ++p) {
        if (!std::isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
    }
    return true;
}
