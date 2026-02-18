#include "app/app_controller.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

#include "app/utils/app_utils.h"
#include "core/log/app_log.h"
#include "core/pipeline/frame_pipeline.h"
#include "core/pool/worker_pool.h"
#include "core/types/fps_tracker.h"
#include "modules/decode/decode_node.h"
#include "modules/display/display_node.h"
#include "modules/source/source_base.h"
#include "modules/source/source_factory.h"
#include "modules/source/source_frame.h"

namespace {

constexpr int kReconnectMaxAttempts = 10;
const auto kReconnectBaseDelay = std::chrono::milliseconds(200);
const auto kReconnectMaxDelay = std::chrono::milliseconds(5000);

struct SourceRuntime {
    SourceConfig cfg;
    std::string window_name;
    WorkerPool pool;
    RunReport report{};
    std::thread th;
    std::unique_ptr<modules::source::SourceBase> source;
    modules::display::DisplayNode display;
    bool init_ok = false;
    bool is_rtsp = false;
};

struct SourceMetrics {
    int total_frames = 0;
    int total_detections = 0;
    double total_infer_ms = 0.0;
    int input_width = 0;
    int input_height = 0;
};

struct AggregatedReport {
    bool any_ok = false;
    int total_frames = 0;
    int total_detections = 0;
    double total_infer_ms = 0.0;
    double total_pipeline_ms = 0.0;
    double max_elapsed_s = 0.0;
    int total_threads = 0;
};

void init_source_report(SourceRuntime* runtime)
{
    if (!runtime)
        return;

    runtime->report = RunReport{};
    runtime->report.type = runtime->is_rtsp ? INPUT_RTSP : runtime->cfg.type;
    runtime->report.threads_use = runtime->cfg.threads;
    runtime->report.output_path = "No out";
    runtime->report.ok = false;
}

void show_source_error(SourceRuntime* runtime, const std::string& message)
{
    if (!runtime)
        return;
    if (runtime->display.ShowError(runtime->window_name, message))
    {
        request_stop();
    }
}

bool reconnect_source(SourceRuntime* runtime,
                      const std::string& input_desc,
                      const char* reason)
{
    if (!runtime || !runtime->source)
        return false;

    std::string msg = "Source error, reconnecting\n";
    msg += input_desc;
    msg += "\n";
    msg += reason;
    show_source_error(runtime, msg);

    LOGW("source error (%s), reconnecting: %s\n", reason, input_desc.c_str());

    auto delay = kReconnectBaseDelay;
    for (int attempt = 1; attempt <= kReconnectMaxAttempts && !stop_requested(); ++attempt)
    {
        if (attempt > 1)
        {
            std::this_thread::sleep_for(delay);
            if (delay < kReconnectMaxDelay)
            {
                auto next = delay * 2;
                delay = next < kReconnectMaxDelay ? next : kReconnectMaxDelay;
            }
        }

        runtime->source->Close();
        if (runtime->source->Open())
        {
            LOGI("source reconnected: %s\n", input_desc.c_str());
            return true;
        }

        LOGW("source reconnect attempt %d/%d failed: %s\n",
             attempt,
             kReconnectMaxAttempts,
             input_desc.c_str());
    }
    return false;
}

bool handle_ready_frame(SourceRuntime* runtime,
                        FrameResult* ready,
                        FpsTracker* fps_tracker,
                        SourceMetrics* metrics)
{
    if (!runtime || !ready || !fps_tracker || !metrics)
        return false;

    metrics->total_infer_ms += ready->infer_ms;
    metrics->total_detections += ready->detections;
    metrics->total_frames++;

    const double fps_display = fps_tracker->on_frame(true);
    const bool exit_pressed = runtime->display.ShowFrame(runtime->window_name,
                                                         &ready->frame,
                                                         fps_display,
                                                         ready->infer_ms);
    if (exit_pressed)
        request_stop();
    return exit_pressed;
}

bool drain_pipeline(SourceRuntime* runtime,
                    FramePipeline* pipeline,
                    FpsTracker* fps_tracker,
                    SourceMetrics* metrics)
{
    if (!runtime || !pipeline || !fps_tracker || !metrics)
        return false;

    while (pipeline->in_flight() > 0)
    {
        FrameResult ready;
        if (pipeline->WaitNextReady(&ready))
        {
            if (handle_ready_frame(runtime, &ready, fps_tracker, metrics))
                return true;
        }
    }

    FrameResult ready;
    while (pipeline->PopReady(&ready))
    {
        if (handle_ready_frame(runtime, &ready, fps_tracker, metrics))
            return true;
    }
    return false;
}

void finalize_source_report(SourceRuntime* runtime,
                            const SourceMetrics& metrics,
                            bool source_ok,
                            bool got_frame,
                            std::chrono::steady_clock::time_point run_t0,
                            std::chrono::steady_clock::time_point run_t1)
{
    if (!runtime)
        return;

    runtime->report.total_frames = metrics.total_frames;
    runtime->report.total_detections = metrics.total_detections;
    runtime->report.total_infer_ms = metrics.total_infer_ms;
    runtime->report.total_pipeline_ms = 0.0;
    runtime->report.total_elapsed_s = std::chrono::duration<double>(run_t1 - run_t0).count();
    runtime->report.input_width = metrics.input_width;
    runtime->report.input_height = metrics.input_height;
    runtime->report.output_path = "No out";
    runtime->report.ok = source_ok && (got_frame || stop_requested());
}

bool run_single_source(SourceRuntime* runtime)
{
    if (!runtime || !runtime->source)
        return false;

    runtime->display.InitWindow(runtime->window_name);

    modules::decode::DecodeNode decode_node;
    FpsTracker fps_tracker;
    FramePipeline pipeline(&runtime->pool.workers, &runtime->pool.results);

    bool source_ok = true;
    bool got_frame = false;
    bool keep_error_window = false;
    SourceMetrics metrics;

    const auto run_t0 = std::chrono::steady_clock::now();
    auto frame_interval = std::chrono::steady_clock::duration::zero();
    if (runtime->cfg.fps > 0.0)
    {
        frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / runtime->cfg.fps));
    }
    auto next_frame_time = std::chrono::steady_clock::now();

    cv::Mat resized;

    if (!runtime->source->Open())
    {
        if (!reconnect_source(runtime, runtime->cfg.input, "open failed"))
        {
            source_ok = false;
            keep_error_window = true;
            std::string msg = "Source reconnect failed\n";
            msg += runtime->cfg.input;
            show_source_error(runtime, msg);
        }
    }

    // Frame data flow per source:
    //   SourceBase::Read -> DecodeNode -> FramePipeline(infer workers) -> DisplayNode
    while (source_ok && !stop_requested())
    {
        if (runtime->cfg.fps > 0.0)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < next_frame_time)
            {
                std::this_thread::sleep_until(next_frame_time);
            }
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
        }

        modules::source::SourceFrame input_frame;
        if (!runtime->source->Read(&input_frame))
        {
            LOGE("source frame capture failed: %s\n", runtime->cfg.input.c_str());
            if (!reconnect_source(runtime, runtime->cfg.input, "capture failed"))
            {
                source_ok = false;
                keep_error_window = true;
                std::string msg = "Source reconnect failed\n";
                msg += runtime->cfg.input;
                show_source_error(runtime, msg);
                break;
            }
            next_frame_time = std::chrono::steady_clock::now() + frame_interval;
            continue;
        }

        cv::Mat decoded;
        if (!decode_node.Decode(input_frame, &decoded) || decoded.empty())
        {
            LOGE("decode failed: %s\n", runtime->cfg.input.c_str());
            if (!reconnect_source(runtime, runtime->cfg.input, "decode failed"))
            {
                source_ok = false;
                keep_error_window = true;
                std::string msg = "Source decode/reconnect failed\n";
                msg += runtime->cfg.input;
                show_source_error(runtime, msg);
                break;
            }
            continue;
        }

        got_frame = true;
        const cv::Mat* use_frame = &decoded;
        const bool allow_resize =
            (runtime->cfg.type != INPUT_CAMERA && !modules::source::IsMipiSource(runtime->cfg));
        if (allow_resize && runtime->cfg.width > 0 && runtime->cfg.height > 0 &&
            (decoded.cols != runtime->cfg.width || decoded.rows != runtime->cfg.height))
        {
            cv::resize(decoded, resized, cv::Size(runtime->cfg.width, runtime->cfg.height));
            use_frame = &resized;
        }

        if (metrics.input_width == 0 && metrics.input_height == 0)
        {
            metrics.input_width = use_frame->cols;
            metrics.input_height = use_frame->rows;
        }

        pipeline.WaitForCapacity();
        pipeline.EnqueueFrame(*use_frame, input_frame.capture_tp);

        FrameResult ready;
        if (pipeline.PopReady(&ready))
        {
            if (handle_ready_frame(runtime, &ready, &fps_tracker, &metrics))
                break;
        }
    }

    drain_pipeline(runtime, &pipeline, &fps_tracker, &metrics);

    const auto run_t1 = std::chrono::steady_clock::now();
    finalize_source_report(runtime, metrics, source_ok, got_frame, run_t0, run_t1);

    runtime->source->Close();
    if (!keep_error_window)
    {
        runtime->display.CloseWindow(runtime->window_name);
    }
    return source_ok;
}

std::unique_ptr<SourceRuntime> build_runtime(const SourceConfig& source_cfg,
                                             const char* model_path)
{
    auto runtime = std::make_unique<SourceRuntime>();
    runtime->cfg = source_cfg;
    runtime->window_name = "dock_blindspot - " + source_cfg.name;
    runtime->is_rtsp = (source_cfg.type == INPUT_RTSP) ||
                       modules::source::IsRtspInput(source_cfg.input);
    runtime->source = modules::source::BuildSource(source_cfg);
    init_source_report(runtime.get());

    if (!runtime->source)
    {
        LOGE("build source failed for [%s] (%s)\n",
             source_cfg.name.c_str(),
             source_cfg.input.c_str());
        return runtime;
    }

    if (!init_worker_pool(model_path,
                          source_cfg.threads,
                          static_cast<float>(source_cfg.conf_threshold),
                          &runtime->pool))
    {
        LOGE("init worker pool failed for [%s] (%s), skipping\n",
             source_cfg.name.c_str(),
             source_cfg.input.c_str());

        std::string msg = "Init worker pool failed\n";
        msg += source_cfg.name;
        msg += "\n";
        msg += source_cfg.input;
        show_source_error(runtime.get(), msg);
        return runtime;
    }

    runtime->init_ok = true;
    return runtime;
}

void launch_source_threads(std::vector<std::unique_ptr<SourceRuntime>>* runtimes)
{
    if (!runtimes)
        return;

    for (auto& rt : *runtimes)
    {
        SourceRuntime* runtime = rt.get();
        if (!runtime || !runtime->init_ok)
            continue;

        runtime->th = std::thread([runtime]() {
            if (!run_single_source(runtime))
            {
                LOGE("source failed: %s (%s)\n",
                     runtime->cfg.name.c_str(),
                     runtime->cfg.input.c_str());
            }
        });
    }
}

void join_source_threads(std::vector<std::unique_ptr<SourceRuntime>>* runtimes)
{
    if (!runtimes)
        return;

    for (auto& rt : *runtimes)
    {
        if (rt && rt->th.joinable())
            rt->th.join();
    }
}

void stop_runtime_pools(std::vector<std::unique_ptr<SourceRuntime>>* runtimes)
{
    if (!runtimes)
        return;

    for (auto& rt : *runtimes)
    {
        if (rt && rt->init_ok)
            stop_worker_pool(&rt->pool);
    }
}

AggregatedReport aggregate_reports(const std::vector<std::unique_ptr<SourceRuntime>>& runtimes)
{
    AggregatedReport agg;

    for (const auto& rt : runtimes)
    {
        if (!rt || !rt->init_ok)
            continue;

        const RunReport& r = rt->report;
        agg.any_ok = agg.any_ok || r.ok;
        agg.total_frames += r.total_frames;
        agg.total_detections += r.total_detections;
        agg.total_infer_ms += r.total_infer_ms;
        agg.total_pipeline_ms += r.total_pipeline_ms;
        if (r.total_elapsed_s > agg.max_elapsed_s)
            agg.max_elapsed_s = r.total_elapsed_s;
        agg.total_threads += rt->cfg.threads;
    }

    return agg;
}

void fill_source_reports(const std::vector<std::unique_ptr<SourceRuntime>>& runtimes,
                         std::vector<SourceRunReport>* source_reports)
{
    if (!source_reports)
        return;

    source_reports->clear();
    source_reports->reserve(runtimes.size());

    for (const auto& rt : runtimes)
    {
        if (!rt)
            continue;

        SourceRunReport item;
        item.name = rt->cfg.name;
        item.report = rt->report;
        item.config_width = rt->cfg.width;
        item.config_height = rt->cfg.height;
        source_reports->push_back(std::move(item));
    }
}

} // namespace

bool AppController::Run(const AppConfig& cfg,
                        const char* model_path,
                        RunReport* report,
                        std::vector<SourceRunReport>* source_reports)
{
    if (!model_path || !report)
        return false;
    if (source_reports)
        source_reports->clear();

    if (cfg.mode_type != INPUT_VIDEO_CAMERA)
    {
        LOGE("mode_image is removed, only video_camera mode is supported\n");
        return false;
    }

    if (cfg.sources.empty())
    {
        LOGE("config missing sources\n");
        return false;
    }

    // AppController::Run only orchestrates stages:
    // 1) source + inference worker init
    // 2) per-source processing thread run
    // 3) join + stop workers
    // 4) aggregate source reports into global report
    std::vector<std::unique_ptr<SourceRuntime>> runtimes;
    runtimes.reserve(cfg.sources.size());

    bool any_init_ok = false;
    for (const auto& source_cfg : cfg.sources)
    {
        auto runtime = build_runtime(source_cfg, model_path);
        if (runtime && runtime->init_ok)
            any_init_ok = true;
        runtimes.push_back(std::move(runtime));
    }

    if (!any_init_ok)
    {
        LOGE("all sources failed to initialize\n");
    }

    launch_source_threads(&runtimes);
    join_source_threads(&runtimes);
    stop_runtime_pools(&runtimes);

    const AggregatedReport agg = aggregate_reports(runtimes);

    report->type = INPUT_VIDEO_CAMERA;
    report->threads_use = agg.total_threads;
    report->total_frames = agg.total_frames;
    report->total_detections = agg.total_detections;
    report->total_infer_ms = agg.total_infer_ms;
    report->total_pipeline_ms = agg.total_pipeline_ms;
    report->total_elapsed_s = agg.max_elapsed_s;
    report->output_path = "No out";
    report->ok = agg.any_ok;

    fill_source_reports(runtimes, source_reports);
    return report->ok;
}
