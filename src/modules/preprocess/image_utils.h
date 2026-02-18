#ifndef _RKNN_MODEL_ZOO_IMAGE_UTILS_H_
#define _RKNN_MODEL_ZOO_IMAGE_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "core/types/common.h"

typedef struct {
    int x_pad;
    int y_pad;
    float scale;
} letterbox_t;

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
