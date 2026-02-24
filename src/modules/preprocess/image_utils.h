#ifndef _RKNN_MODEL_ZOO_IMAGE_UTILS_H_
#define _RKNN_MODEL_ZOO_IMAGE_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "core/types/common.h"
#include "core/types/preprocess_types.h"

// Preprocess core API.
int convert_image_with_letterbox(image_buffer_t* src_image,
                                 image_buffer_t* dst_image,
                                 letterbox_t* letterbox,
                                 char color);

int get_image_size(image_buffer_t* image);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _RKNN_MODEL_ZOO_IMAGE_UTILS_H_
