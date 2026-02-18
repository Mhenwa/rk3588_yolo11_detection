#ifndef MODULES_INFERENCE_INFER_NODE_H_
#define MODULES_INFERENCE_INFER_NODE_H_

#include <vector>

#include "core/types/common.h"
#include "modules/inference/infer_context.h"

namespace modules {
namespace inference {

struct InferOutput {
    std::vector<rknn_output> raw_outputs;
    double infer_ms = 0.0;
};

class InferNode {
public:
    bool Run(rknn_app_context_t* app_ctx,
             const image_buffer_t& input_image,
             InferOutput* out) const;
};

}  // namespace inference
}  // namespace modules

#endif  // MODULES_INFERENCE_INFER_NODE_H_
