#include <csignal>
#include <cstring>
#include <string>
#include <vector>

#include "app_config.h"
#include "app_log.h"
#include "app_modes.h"
#include "app_utils.h"
#include "postprocess.h"
#include "video_camera_runtime.h"

namespace {
struct LogGuard {
    LogGuard() { init_logging(); }
    ~LogGuard() { shutdown_logging(); }
};
}  // namespace

int main(int argc, char **argv)
{
    LogGuard log_guard;
    std::signal(SIGINT, handle_sigint);

    std::string config_path = "config.json";
    if (argc == 2) {
        config_path = argv[1];
    } else if (argc == 3 &&
               (strcmp(argv[1], "--config") == 0 || strcmp(argv[1], "-c") == 0)) {
        config_path = argv[2];
    } else if (argc != 1) {
        print_usage(argv[0]);
        return -1;
    }

    AppConfig cfg;
    std::string error;
    if (!load_config(config_path, &cfg, &error)) {
        if (!error.empty()) {
            LOGE("%s\n", error.c_str());
        }
        print_usage(argv[0]);
        return -1;
    }

    const char* model_path = cfg.model_path.c_str();
    const char* label_path = cfg.label_path.c_str();

    RunReport report;
    memset(&report, 0, sizeof(RunReport));
    report.type = cfg.mode_type;
    report.ok = false;
    report.threads_use = 1;
    std::vector<SourceRunReport> source_reports;

    rknn_app_context_t single_ctx;
    bool single_ctx_inited = false;

    if (init_post_process(label_path) != 0) {
        goto out;
    }

    if (cfg.mode_type == INPUT_VIDEO_CAMERA) {
        if (!run_video_camera_mode(cfg.sources, model_path, &report,
                                   &source_reports)) {
            goto out;
        }
        goto out;
    }

    memset(&single_ctx, 0, sizeof(rknn_app_context_t));
    if (init_yolo11_model(model_path, &single_ctx) != 0) {
        LOGE("init_yolo11_model failed\n");
        goto out;
    }
    single_ctx_inited = true;

    if (cfg.mode_type == INPUT_IMAGE) {
        if (!run_image_mode(cfg.input.c_str(), &single_ctx, &report)) {
            goto out;
        }
    } else {
        LOGE("invalid mode in config\n");
        goto out;
    }

out:
    if (single_ctx_inited) {
        release_yolo11_model(&single_ctx);
    }
    deinit_post_process();
    print_run_report(report, &source_reports);
    return 0;
}
