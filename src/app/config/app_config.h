#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <string>
#include <vector>

#include "core/types/app_core_types.h"
#include "modules/display/display.h"
#include "nlohmann/json.hpp"

constexpr double kDefaultConfThreshold = 0.25; // Default confidence threshold.

// 每一路的配置
struct SourceConfig
{
    std::string name; // 名称，注意不能重复，目前的cv窗口靠名字区分
    InputType type = INPUT_VIDEO; // 模式：video、rtsp、mipi_camera、usb_camera
    std::string input; // 输入路径/设备/URL
    int threads = 3;
    int width = 0;
    int height = 0;
    double fps = 30.0;
    std::string format = "auto"; // USB: auto/mjpeg/yuyv; MIPI: auto/nv12/yuyv; rtsp: auto/h264/h265
    double conf_threshold = kDefaultConfThreshold; // 置信度阈值，低于不画框

    // 此项是否为默认设置
    bool threads_set = false;
    bool width_set = false;
    bool height_set = false;
    bool fps_set = false;
    bool format_set = false;
    bool conf_threshold_set = false;
};

struct AppConfig
{
    std::string model_path;
    std::string label_path;
    InputType mode_type = INPUT_VIDEO_CAMERA; // 事实上只有这一个，原来的模式被删除了
    int gtk_window_width = DISPLAY_WALL_WIDTH;
    int gtk_window_height = DISPLAY_WALL_HEIGHT;
    bool gtk_window_fullscreen = false;

    std::vector<SourceConfig> sources; // 多路的各路配置
};

/*
    解析config
*/
bool parse_config(const nlohmann::json &root,
                  AppConfig *cfg,
                  std::string *error);
/*
    加载并解析config，内部会调用parse_config
*/
bool load_config(const std::string &path,
                 AppConfig *cfg,
                 std::string *error);

#endif // APP_CONFIG_H_
