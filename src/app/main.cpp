#include <csignal>
#include <cstring>
#include <string>
#include <vector>

#include "app/app_controller.h"
#include "app/config/app_config.h"
#include "app/utils/app_utils.h"
#include "core/log/app_log.h"
#include "modules/postprocess/postprocess_node.h"

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
    reset_stop_flag();

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

    if (init_post_process(label_path) != 0) {
        print_run_report(report, &source_reports);
        return -1;
    }

    AppController controller;
    controller.Run(cfg, model_path, &report, &source_reports);

    deinit_post_process();
    print_run_report(report, &source_reports);
    return report.ok ? 0 : -1;
}
