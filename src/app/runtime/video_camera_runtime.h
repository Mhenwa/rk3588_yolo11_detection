#ifndef VIDEO_CAMERA_RUNTIME_H_
#define VIDEO_CAMERA_RUNTIME_H_

#include <vector>

#include "app_config.h"
#include "app_core_types.h"

bool run_video_camera_mode(const std::vector<SourceConfig>& sources,
                           const char* model_path,
                           RunReport* report,
                           std::vector<SourceRunReport>* source_reports);

#endif  // VIDEO_CAMERA_RUNTIME_H_
