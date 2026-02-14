#ifndef APP_CORE_TYPES_H_
#define APP_CORE_TYPES_H_

#include <string>

enum InputType {
    INPUT_IMAGE,
    INPUT_VIDEO,
    INPUT_CAMERA,
    INPUT_VIDEO_CAMERA
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

#endif  // APP_CORE_TYPES_H_
