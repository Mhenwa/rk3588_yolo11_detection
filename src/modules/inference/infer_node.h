#ifndef MODULES_INFERENCE_INFER_NODE_H_
#define MODULES_INFERENCE_INFER_NODE_H_

#include "core/types/vision_types.h"
#include "core/types/infer_types.h"

namespace modules {
namespace inference {

using InferOutput = core::types::InferOutput;

class InferNode {
public:
    bool Run(rknn_app_context_t* app_ctx,
             const image_buffer_t& input_image,
             core::types::InferOutput* out) const;
};

}  // namespace inference
}  // namespace modules

#endif  // MODULES_INFERENCE_INFER_NODE_H_
