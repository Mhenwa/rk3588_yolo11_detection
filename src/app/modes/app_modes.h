#ifndef APP_MODES_H_
#define APP_MODES_H_

#include <string>

#include "app_types.h"

struct WorkerPool;

bool run_image_mode(const char* input_arg,
                    rknn_app_context_t* ctx,
                    RunReport* report);

bool run_video_mode(const char* input_arg,
                    WorkerPool* pool,
                    RunReport* report,
                    const char* window_name,
                    double max_fps,
                    int resize_width,
                    int resize_height);

bool run_camera_mode(const char* input_arg,
                     WorkerPool* pool,
                     RunReport* report,
                     const char* window_name,
                     double max_fps,
                     int width,
                     int height,
                     int buffers,
                     double fps,
                     const std::string& format);

bool run_rtsp_mode(const char* input_arg,
                   WorkerPool* pool,
                   RunReport* report,
                   const char* window_name,
                   double max_fps,
                   int resize_width,
                   int resize_height,
                   const std::string& codec);

#endif  // APP_MODES_H_
