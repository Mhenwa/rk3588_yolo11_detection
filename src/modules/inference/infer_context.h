#ifndef MODULES_INFERENCE_INFER_CONTEXT_H_
#define MODULES_INFERENCE_INFER_CONTEXT_H_

#include "core/types/infer_types.h"

int init_infer_context(const char* model_path, rknn_app_context_t* app_ctx);
int release_infer_context(rknn_app_context_t* app_ctx);

#endif  // MODULES_INFERENCE_INFER_CONTEXT_H_
