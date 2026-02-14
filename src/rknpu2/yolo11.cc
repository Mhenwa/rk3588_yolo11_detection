// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "app_log.h"
#include "yolo11.h"
#include "common.h"
#include "file_utils.h"
#include "image_utils.h"

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    LOGI("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
         attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

struct ModelInfo {
    rknn_input_output_num io_num;
    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    bool is_quant;
    ModelInfo()
        : io_num{}, model_channel(0), model_width(0), model_height(0), is_quant(false) {}
};

struct SharedModelCache {
    std::string model_path;
    rknn_context base_ctx = 0;
    ModelInfo info;
    int ref_count = 0;
    bool share_enabled = true;
};

static std::mutex g_shared_model_mtx;
static SharedModelCache g_shared_model;

static int init_context_from_file(const char *model_path, rknn_context *ctx_out)
{
    int ret;
    int model_len = 0;
    char *model;

    model_len = read_data_from_file(model_path, &model);
    if (model == NULL)
    {
        LOGE("load_model fail!\n");
        return -1;
    }

    rknn_context ctx = 0;
    ret = rknn_init(&ctx, model, model_len, 0, NULL);
    free(model);
    if (ret < 0)
    {
        LOGE("rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    *ctx_out = ctx;
    return 0;
}

static int query_model_info(rknn_context ctx, ModelInfo *info, bool log_details)
{
    int ret;
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC)
    {
        LOGE("rknn_query fail! ret=%d\n", ret);
        return -1;
    }
    if (log_details)
    {
        LOGI("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);
    }

    if (log_details)
    {
        LOGI("input tensors:\n");
    }
    std::vector<rknn_tensor_attr> input_attrs(io_num.n_input);
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            LOGE("rknn_query fail! ret=%d\n", ret);
            return -1;
        }
        if (log_details)
        {
            dump_tensor_attr(&(input_attrs[i]));
        }
    }

    if (log_details)
    {
        LOGI("output tensors:\n");
    }
    std::vector<rknn_tensor_attr> output_attrs(io_num.n_output);
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            LOGE("rknn_query fail! ret=%d\n", ret);
            return -1;
        }
        if (log_details)
        {
            dump_tensor_attr(&(output_attrs[i]));
        }
    }

    bool is_quant = false;
    if (io_num.n_output > 0 &&
        output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        output_attrs[0].type == RKNN_TENSOR_INT8)
    {
        is_quant = true;
    }

    int model_channel = 0;
    int model_width = 0;
    int model_height = 0;
    if (io_num.n_input > 0 && input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        if (log_details)
        {
            LOGI("model is NCHW input fmt\n");
        }
        model_channel = input_attrs[0].dims[1];
        model_height = input_attrs[0].dims[2];
        model_width = input_attrs[0].dims[3];
    }
    else if (io_num.n_input > 0)
    {
        if (log_details)
        {
            LOGI("model is NHWC input fmt\n");
        }
        model_height = input_attrs[0].dims[1];
        model_width = input_attrs[0].dims[2];
        model_channel = input_attrs[0].dims[3];
    }
    if (log_details)
    {
        LOGI("model input height=%d, width=%d, channel=%d\n",
             model_height, model_width, model_channel);
    }

    info->io_num = io_num;
    info->input_attrs = std::move(input_attrs);
    info->output_attrs = std::move(output_attrs);
    info->model_channel = model_channel;
    info->model_width = model_width;
    info->model_height = model_height;
    info->is_quant = is_quant;

    return 0;
}

static void fill_app_ctx_from_info(rknn_app_context_t *app_ctx, const ModelInfo &info)
{
    app_ctx->io_num = info.io_num;
    app_ctx->input_attrs = nullptr;
    app_ctx->output_attrs = nullptr;
    if (!info.input_attrs.empty())
    {
        size_t bytes = info.input_attrs.size() * sizeof(rknn_tensor_attr);
        app_ctx->input_attrs = (rknn_tensor_attr *)malloc(bytes);
        memcpy(app_ctx->input_attrs, info.input_attrs.data(), bytes);
    }
    if (!info.output_attrs.empty())
    {
        size_t bytes = info.output_attrs.size() * sizeof(rknn_tensor_attr);
        app_ctx->output_attrs = (rknn_tensor_attr *)malloc(bytes);
        memcpy(app_ctx->output_attrs, info.output_attrs.data(), bytes);
    }
    app_ctx->model_channel = info.model_channel;
    app_ctx->model_width = info.model_width;
    app_ctx->model_height = info.model_height;
    app_ctx->is_quant = info.is_quant;
}

static void reset_shared_model(SharedModelCache *cache)
{
    if (!cache) return;
    if (cache->base_ctx != 0)
    {
        rknn_destroy(cache->base_ctx);
        cache->base_ctx = 0;
    }
    cache->model_path.clear();
    cache->info = ModelInfo();
    cache->ref_count = 0;
}

static int ensure_shared_model(const char *model_path, SharedModelCache *cache)
{
    if (!cache->share_enabled) return -1;
    if (cache->base_ctx != 0)
    {
        if (cache->model_path == model_path)
        {
            return 0;
        }
        if (cache->ref_count != 0)
        {
            LOGE("shared model already in use with different model path\n");
            return -1;
        }
        reset_shared_model(cache);
    }

    rknn_context ctx = 0;
    if (init_context_from_file(model_path, &ctx) != 0)
    {
        return -1;
    }
    ModelInfo info;
    if (query_model_info(ctx, &info, true) != 0)
    {
        rknn_destroy(ctx);
        return -1;
    }
    cache->model_path = model_path;
    cache->base_ctx = ctx;
    cache->info = std::move(info);
    return 0;
}

static int init_yolo11_model_standalone(const char *model_path, rknn_app_context_t *app_ctx)
{
    rknn_context ctx = 0;
    if (init_context_from_file(model_path, &ctx) != 0)
    {
        return -1;
    }
    ModelInfo info;
    if (query_model_info(ctx, &info, true) != 0)
    {
        rknn_destroy(ctx);
        return -1;
    }
    app_ctx->rknn_ctx = ctx;
    fill_app_ctx_from_info(app_ctx, info);
    return 0;
}

int init_yolo11_model(const char *model_path, rknn_app_context_t *app_ctx)
{
    if (!model_path || !app_ctx)
    {
        return -1;
    }
    app_ctx->shared_handle = nullptr;

    {
        std::lock_guard<std::mutex> lk(g_shared_model_mtx);
        if (g_shared_model.share_enabled)
        {
            if (ensure_shared_model(model_path, &g_shared_model) == 0)
            {
                rknn_context ctx = 0;
                int ret = rknn_dup_context(&g_shared_model.base_ctx, &ctx);
                if (ret == RKNN_SUCC)
                {
                    app_ctx->rknn_ctx = ctx;
                    fill_app_ctx_from_info(app_ctx, g_shared_model.info);
                    app_ctx->shared_handle = &g_shared_model;
                    g_shared_model.ref_count++;
                    LOGI("reuse model weights: %s (shared contexts=%d)\n",
                         model_path, g_shared_model.ref_count);
                    return 0;
                }
                LOGW("rknn_dup_context failed (ret=%d), fallback to per-context init\n", ret);
                g_shared_model.share_enabled = false;
                if (g_shared_model.ref_count == 0)
                {
                    reset_shared_model(&g_shared_model);
                }
            }
        }
    }

    return init_yolo11_model_standalone(model_path, app_ctx);
}

// int init_yolo11_model_with_shared_ctx(const char *model_path,
//                                       rknn_app_context_t *app_ctx,
//                                       rknn_context *shared_ctx)
// {
//     return init_yolo11_model_internal(model_path, app_ctx, shared_ctx);
// }

// int init_yolo11_model(const char *model_path, rknn_app_context_t *app_ctx)
// {
//     return init_yolo11_model_internal(model_path, app_ctx, NULL);
// }

int release_yolo11_model(rknn_app_context_t *app_ctx)
{
    if (app_ctx->input_attrs != NULL)
    {
        free(app_ctx->input_attrs);
        app_ctx->input_attrs = NULL;
    }
    if (app_ctx->output_attrs != NULL)
    {
        free(app_ctx->output_attrs);
        app_ctx->output_attrs = NULL;
    }
    if (app_ctx->rknn_ctx != 0)
    {
        rknn_destroy(app_ctx->rknn_ctx);
        app_ctx->rknn_ctx = 0;
    }
    if (app_ctx->shared_handle != nullptr)
    {
        std::lock_guard<std::mutex> lk(g_shared_model_mtx);
        auto *cache = static_cast<SharedModelCache *>(app_ctx->shared_handle);
        if (cache->ref_count > 0)
        {
            cache->ref_count--;
        }
        if (cache->ref_count == 0 && cache->base_ctx != 0)
        {
            reset_shared_model(cache);
        }
        app_ctx->shared_handle = nullptr;
    }
    return 0;
}

int inference_yolo11_model(rknn_app_context_t *app_ctx,
                           image_buffer_t *img,
                           object_detect_result_list *od_results,
                           float conf_threshold)
{
    int ret;
    image_buffer_t dst_img;
    letterbox_t letter_box;
    rknn_input inputs[app_ctx->io_num.n_input];
    rknn_output outputs[app_ctx->io_num.n_output];
    const float nms_threshold = NMS_THRESH;      // 默认的NMS阈值
    const float box_conf_threshold = conf_threshold;
    int bg_color = 114;

    if ((!app_ctx) || !(img) || (!od_results))
    {
        return -1;
    }

    memset(od_results, 0x00, sizeof(*od_results));
    memset(&letter_box, 0, sizeof(letterbox_t));
    memset(&dst_img, 0, sizeof(image_buffer_t));
    memset(inputs, 0, sizeof(inputs));
    memset(outputs, 0, sizeof(outputs));

    // Pre Process
    dst_img.width = app_ctx->model_width;
    dst_img.height = app_ctx->model_height;
    // dst_img.width_stride = dst_img.width;
    // dst_img.height_stride = dst_img.height;
    dst_img.format = IMAGE_FORMAT_RGB888;
    dst_img.size = get_image_size(&dst_img);
    dst_img.virt_addr = (unsigned char *)malloc(dst_img.size);
    if (dst_img.virt_addr == NULL)
    {
        LOGE("malloc buffer size:%d fail!\n", dst_img.size);
        return -1;
    }

    // letterbox
    ret = convert_image_with_letterbox(img, &dst_img, &letter_box, bg_color);
    if (ret < 0)
    {
        LOGE("convert_image_with_letterbox fail! ret=%d\n", ret);
        return -1;
    }

    // Set Input Data
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;
    inputs[0].buf = dst_img.virt_addr;

    ret = rknn_inputs_set(app_ctx->rknn_ctx, app_ctx->io_num.n_input, inputs);
    if (ret < 0)
    {
        LOGE("rknn_input_set fail! ret=%d\n", ret);
        return -1;
    }

    // Run
    LOGI("rknn_run\n");
    ret = rknn_run(app_ctx->rknn_ctx, nullptr);
    if (ret < 0)
    {
        LOGE("rknn_run fail! ret=%d\n", ret);
        return -1;
    }

    // Get Output
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < app_ctx->io_num.n_output; i++)
    {
        outputs[i].index = i;
        outputs[i].want_float = (!app_ctx->is_quant);
    }
    ret = rknn_outputs_get(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs, NULL);
    if (ret < 0)
    {
        LOGE("rknn_outputs_get fail! ret=%d\n", ret);
        goto out;
    }

    // Post Process
    post_process(app_ctx, outputs, &letter_box, box_conf_threshold, nms_threshold, od_results);

    // Remeber to release rknn output
    rknn_outputs_release(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs);

out:
    if (dst_img.virt_addr != NULL)
    {
        free(dst_img.virt_addr);
    }

    return ret;
}
