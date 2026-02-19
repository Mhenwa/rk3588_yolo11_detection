#ifndef CORE_TYPES_APP_CORE_TYPES_H_
#define CORE_TYPES_APP_CORE_TYPES_H_

#include <string>

enum InputType {
    INPUT_IMAGE, // 废弃
    INPUT_VIDEO,
    INPUT_USB_CAMERA,
    INPUT_MIPI_CAMERA,
    INPUT_RTSP,
    INPUT_VIDEO_CAMERA // 废弃
};

struct RunReport {
    InputType type;
    int total_frames;
    int total_detections;
    int threads_use;
    double total_infer_ms;
    double total_pipeline_ms;
    double total_elapsed_s;
    int input_width;
    int input_height;
    const char* output_path;
    bool ok;
};

struct SourceRunReport {
    std::string name;
    RunReport report;
    int config_width;
    int config_height;
};

#endif  // CORE_TYPES_APP_CORE_TYPES_H_
