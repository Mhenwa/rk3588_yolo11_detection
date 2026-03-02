#ifndef MODULES_PREPROCESS_IMAGE_PREPROCESS_UTILS_H_
#define MODULES_PREPROCESS_IMAGE_PREPROCESS_UTILS_H_

#include "core/types/vision_types.h"

// Preprocess core API.
int convert_image_with_letterbox(image_buffer_t* src_image,
                                 image_buffer_t* dst_image,
                                 letterbox_t* letterbox,
                                 char color);

int get_image_size(image_buffer_t* image);

#endif  // MODULES_PREPROCESS_IMAGE_PREPROCESS_UTILS_H_
