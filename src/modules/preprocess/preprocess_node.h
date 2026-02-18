#ifndef MODULES_PREPROCESS_PREPROCESS_NODE_H_
#define MODULES_PREPROCESS_PREPROCESS_NODE_H_

#include <vector>

#include <opencv2/opencv.hpp>

#include "modules/preprocess/image_utils.h"
#include "modules/inference/infer_context.h"

namespace modules {
namespace preprocess {

struct PreprocessOutput {
    image_buffer_t input_image;
    letterbox_t letterbox;
    std::vector<unsigned char> input_storage;
};

class PreprocessNode {
public:
    bool Run(const cv::Mat& frame_bgr,
             const rknn_app_context_t& app_ctx,
             PreprocessOutput* out) const;
};

}  // namespace preprocess
}  // namespace modules

#endif  // MODULES_PREPROCESS_PREPROCESS_NODE_H_
