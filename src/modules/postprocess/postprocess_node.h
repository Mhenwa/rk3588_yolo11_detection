#ifndef MODULES_POSTPROCESS_POSTPROCESS_NODE_H_
#define MODULES_POSTPROCESS_POSTPROCESS_NODE_H_

#include <stdint.h>

#include <opencv2/opencv.hpp>

#include "core/types/common.h"
#include "core/types/infer_types.h"
#include "core/types/preprocess_types.h"

constexpr int OBJ_NUMB_MAX_SIZE = 128;
constexpr int OBJ_CLASS_NUM = 7;
constexpr float NMS_THRESH = 0.45f;

typedef struct {
    image_rect_t box;
    float prop;
    int cls_id;
} object_detect_result;

typedef struct {
    int id;
    int count;
    object_detect_result results[OBJ_NUMB_MAX_SIZE];
} object_detect_result_list;

int init_post_process(const char* label_path);
void deinit_post_process();
char* coco_cls_to_name(int cls_id);

namespace modules {
namespace postprocess {

struct PostprocessOutput {
    object_detect_result_list detections;
    int detection_count = 0;
};

class PostprocessNode {
public:
    bool Run(rknn_app_context_t* app_ctx,
             core::types::InferOutput* infer_output,
             const letterbox_t& letterbox,
             float conf_threshold,
             cv::Mat* frame,
             PostprocessOutput* out) const;
};

}  // namespace postprocess
}  // namespace modules

#endif  // MODULES_POSTPROCESS_POSTPROCESS_NODE_H_
