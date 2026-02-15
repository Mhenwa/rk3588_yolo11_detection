#include "video_camera_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <thread>

#include "app_log.h"
#include "app_modes.h"
#include "app_utils.h"
#include "worker_pool.h"

namespace {
bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool is_rtsp_input(const std::string& input)
{
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return starts_with(lower, "rtsp://") || starts_with(lower, "rtsps://");
}

struct SourceRuntime {
    SourceConfig cfg;
    std::string window_name;
    WorkerPool pool;
    RunReport report;
    std::thread th;
    bool init_ok = false;
    bool is_rtsp_source = false;
};
}  // namespace

bool run_video_camera_mode(const std::vector<SourceConfig>& sources,
                           const char* model_path,
                           RunReport* report,
                           std::vector<SourceRunReport>* source_reports)
{
    if (!report) return false;
    if (source_reports) source_reports->clear();
    if (sources.empty()) {
        LOGE("config missing [video_camera].sources\n");
        return false;
    }

    std::vector<std::unique_ptr<SourceRuntime>> runtimes;
    runtimes.reserve(sources.size());
    bool any_init_ok = false;
    for (const auto& source : sources) {
        auto runtime = std::make_unique<SourceRuntime>();
        runtime->cfg = source;
        runtime->is_rtsp_source = (source.type == INPUT_RTSP) ||
                                  is_rtsp_input(source.input);
        runtime->window_name = "dock_blindspot - " + source.name;
        memset(&runtime->report, 0, sizeof(RunReport));
        runtime->report.type = runtime->is_rtsp_source ? INPUT_RTSP : source.type;
        runtime->report.threads_use = source.threads;
        runtime->report.ok = false;
        if (source.type != INPUT_CAMERA && source.buffers_set) {
            LOGW("buffers is only valid for camera sources, ignored for [%s]\n",
                 source.name.c_str());
        }
        if (source.type == INPUT_VIDEO && source.format_set &&
            !runtime->is_rtsp_source) {
            LOGW("format is only valid for camera/rtsp sources, ignored for [%s]\n",
                 source.name.c_str());
        }
        if (!init_worker_pool(model_path, source.threads,
                              static_cast<float>(source.conf_threshold),
                              &runtime->pool)) {
            LOGE("init worker pool failed for [%s] (%s), skipping\n",
                 source.name.c_str(), source.input.c_str());
            std::string msg = "Init worker pool failed\n";
            msg += source.name;
            msg += "\n";
            msg += source.input;
            show_error_window(runtime->window_name.c_str(), msg);
            runtimes.push_back(std::move(runtime));
            continue;
        }
        runtime->init_ok = true;
        any_init_ok = true;
        runtimes.push_back(std::move(runtime));
    }
    if (!any_init_ok) {
        LOGE("all sources failed to initialize\n");
    }

    for (auto& rt : runtimes) {
        SourceRuntime* runtime = rt.get();
        if (!runtime->init_ok) continue;
        runtime->th = std::thread([runtime]() {
            bool ok = false;
            if (runtime->is_rtsp_source) {
                ok = run_rtsp_mode(runtime->cfg.input.c_str(),
                                   &runtime->pool,
                                   &runtime->report,
                                   runtime->window_name.c_str(),
                                   runtime->cfg.fps,
                                   runtime->cfg.width,
                                   runtime->cfg.height,
                                   runtime->cfg.format);
            } else if (runtime->cfg.type == INPUT_VIDEO) {
                ok = run_video_mode(runtime->cfg.input.c_str(),
                                    &runtime->pool,
                                    &runtime->report,
                                    runtime->window_name.c_str(),
                                    runtime->cfg.fps,
                                    runtime->cfg.width,
                                    runtime->cfg.height);
            } else {
                ok = run_camera_mode(runtime->cfg.input.c_str(),
                                     &runtime->pool,
                                     &runtime->report,
                                     runtime->window_name.c_str(),
                                     runtime->cfg.fps,
                                     runtime->cfg.width,
                                     runtime->cfg.height,
                                     runtime->cfg.buffers,
                                     runtime->cfg.fps,
                                     runtime->cfg.format);
            }
            if (!ok) {
                LOGE("source failed: %s (%s)\n",
                     runtime->cfg.name.c_str(),
                     runtime->cfg.input.c_str());
            }
        });
    }

    for (auto& rt : runtimes) {
        if (rt->th.joinable()) rt->th.join();
    }

    for (auto& rt : runtimes) {
        if (!rt->init_ok) continue;
        stop_worker_pool(&rt->pool);
    }

    bool any_ok = false;
    int total_frames = 0;
    int total_detections = 0;
    double total_infer_ms = 0.0;
    double total_pipeline_ms = 0.0;
    double max_elapsed_s = 0.0;
    int total_threads = 0;

    for (const auto& rt : runtimes) {
        const RunReport& r = rt->report;
        if (!rt->init_ok) continue;
        any_ok = any_ok || r.ok;
        total_frames += r.total_frames;
        total_detections += r.total_detections;
        total_infer_ms += r.total_infer_ms;
        total_pipeline_ms += r.total_pipeline_ms;
        if (r.total_elapsed_s > max_elapsed_s) {
            max_elapsed_s = r.total_elapsed_s;
        }
        total_threads += rt->cfg.threads;
    }

    report->type = INPUT_VIDEO_CAMERA;
    report->threads_use = total_threads;
    report->total_frames = total_frames;
    report->total_detections = total_detections;
    report->total_infer_ms = total_infer_ms;
    report->total_pipeline_ms = total_pipeline_ms;
    report->total_elapsed_s = max_elapsed_s;
    report->output_path = "No out";
    report->ok = any_ok;
    if (source_reports) {
        source_reports->clear();
        source_reports->reserve(runtimes.size());
        for (const auto& rt : runtimes) {
            SourceRunReport item;
            item.name = rt->cfg.name;
            item.report = rt->report;
            item.config_width = rt->cfg.width;
            item.config_height = rt->cfg.height;
            source_reports->push_back(std::move(item));
        }
    }
    return report->ok;
}
