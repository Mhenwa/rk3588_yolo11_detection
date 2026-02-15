#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <string>
#include <vector>

#include "app_core_types.h"
#include "nlohmann/json.hpp"

constexpr double kDefaultConfThreshold = 0.25;  // Keep in sync with BOX_THRESH.

struct SourceConfig {
    std::string name;
    InputType type = INPUT_VIDEO;
    std::string input;
    int threads = 3;
    int width = 0;
    int height = 0;
    int buffers = 0;
    double fps = 30.0;
    std::string format = "auto";  // camera pixel format or rtsp codec(h264/h265/auto)
    double conf_threshold = kDefaultConfThreshold;
    bool threads_set = false;
    bool width_set = false;
    bool height_set = false;
    bool buffers_set = false;
    bool fps_set = false;
    bool format_set = false;
    bool conf_threshold_set = false;
};

struct AppConfig {
    std::string model_path;
    std::string label_path;
    InputType mode_type = INPUT_IMAGE;
    std::string input;
    std::vector<SourceConfig> sources;
};

bool parse_config(const nlohmann::json& root,
                  AppConfig* cfg,
                  std::string* error);

bool load_config(const std::string& path,
                 AppConfig* cfg,
                 std::string* error);

#endif  // APP_CONFIG_H_
