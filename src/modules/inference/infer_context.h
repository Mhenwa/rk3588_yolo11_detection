#ifndef MODULES_INFERENCE_INFER_CONTEXT_H_
#define MODULES_INFERENCE_INFER_CONTEXT_H_

#include <rknn_api.h>

// Runtime context for rknpu2 int8 inference on rk3588/rk3576.
typedef struct rknn_app_context_s {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    bool is_quant;
    void* shared_handle;
} rknn_app_context_t;

int init_infer_context(const char* model_path, rknn_app_context_t* app_ctx);
int release_infer_context(rknn_app_context_t* app_ctx);

#endif  // MODULES_INFERENCE_INFER_CONTEXT_H_
