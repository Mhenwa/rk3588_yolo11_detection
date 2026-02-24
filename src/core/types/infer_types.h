#ifndef CORE_TYPES_INFER_TYPES_H_
#define CORE_TYPES_INFER_TYPES_H_

#include <vector>

#include <rknn_api.h>

// Runtime context for rknpu2 int8 inference on rk3588/rk3576.
typedef struct rknn_app_context_s
{
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    bool is_quant;
    void *shared_handle;
} rknn_app_context_t;

namespace core
{
    namespace types
    {

        struct InferOutput
        {
            std::vector<rknn_output> raw_outputs;
            double infer_ms = 0.0;
        };

    } // namespace types
} // namespace core

#endif // CORE_TYPES_INFER_TYPES_H_
