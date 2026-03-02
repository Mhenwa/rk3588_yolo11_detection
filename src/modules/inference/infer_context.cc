#include "modules/inference/infer_context.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/log/app_log.h"

namespace {

void dump_tensor_attr(rknn_tensor_attr* attr)
{
    if (!attr) return;
    char dims[128] = {0};
    for (int i = 0; i < attr->n_dims; ++i) {
        const int idx = static_cast<int>(strlen(dims));
        snprintf(&dims[idx], sizeof(dims) - idx,
                 "%d%s", attr->dims[i], (i == attr->n_dims - 1) ? "" : ", ");
    }
    LOGI("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, "
         "w_stride=%d, size_with_stride=%d, fmt=%s, type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index,
         attr->name,
         attr->n_dims,
         dims,
         attr->n_elems,
         attr->size,
         attr->w_stride,
         attr->size_with_stride,
         get_format_string(attr->fmt),
         get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type),
         attr->zp,
         attr->scale);
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
        : io_num{}, model_channel(0), model_width(0), model_height(0), is_quant(false)
    {
    }
};

struct SharedModelCache {
    std::string model_path;
    rknn_context base_ctx = 0;
    ModelInfo info;
    int ref_count = 0;
    bool share_enabled = true;
};

std::mutex g_shared_model_mtx;
SharedModelCache g_shared_model;

int read_data_from_file_local(const char* path, char** out_data)
{
    if (!path || !out_data) return -1;

    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        LOGE("fopen %s fail!\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    const int file_size = static_cast<int>(ftell(fp));
    char* data = static_cast<char*>(malloc(file_size + 1));
    if (!data) {
        fclose(fp);
        return -1;
    }
    data[file_size] = 0;
    fseek(fp, 0, SEEK_SET);
    if (file_size != static_cast<int>(fread(data, 1, file_size, fp))) {
        LOGE("fread %s fail!\n", path);
        free(data);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out_data = data;
    return file_size;
}

int init_context_from_file(const char* model_path, rknn_context* ctx_out)
{
    if (!model_path || !ctx_out) return -1;

    int model_len = 0;
    char* model = nullptr;
    model_len = read_data_from_file_local(model_path, &model);
    if (!model) {
        LOGE("load_model fail!\n");
        return -1;
    }

    rknn_context ctx = 0;
    const int ret = rknn_init(&ctx, model, model_len, 0, NULL);
    free(model);
    if (ret < 0) {
        LOGE("rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    *ctx_out = ctx;
    return 0;
}

int query_model_info(rknn_context ctx, ModelInfo* info, bool log_details)
{
    if (!info) return -1;

    rknn_input_output_num io_num;
    int ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        LOGE("rknn_query RKNN_QUERY_IN_OUT_NUM fail! ret=%d\n", ret);
        return -1;
    }
    if (log_details) {
        LOGI("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);
    }

    std::vector<rknn_tensor_attr> input_attrs(io_num.n_input);
    std::vector<rknn_tensor_attr> output_attrs(io_num.n_output);

    if (log_details) LOGI("input tensors:\n");
    for (uint32_t i = 0; i < io_num.n_input; ++i) {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOGE("rknn_query RKNN_QUERY_INPUT_ATTR fail! ret=%d\n", ret);
            return -1;
        }
        if (log_details) dump_tensor_attr(&(input_attrs[i]));
    }

    if (log_details) LOGI("output tensors:\n");
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOGE("rknn_query RKNN_QUERY_OUTPUT_ATTR fail! ret=%d\n", ret);
            return -1;
        }
        if (log_details) dump_tensor_attr(&(output_attrs[i]));
    }

    const bool is_quant = (io_num.n_output > 0 &&
                           output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
                           output_attrs[0].type == RKNN_TENSOR_INT8);
    if (!is_quant) {
        LOGE("only int8 quantized rknpu2 models are supported in this build\n");
        return -1;
    }

    int model_channel = 0;
    int model_width = 0;
    int model_height = 0;
    if (io_num.n_input > 0 && input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        if (log_details) LOGI("model is NCHW input fmt\n");
        model_channel = input_attrs[0].dims[1];
        model_height = input_attrs[0].dims[2];
        model_width = input_attrs[0].dims[3];
    } else if (io_num.n_input > 0) {
        if (log_details) LOGI("model is NHWC input fmt\n");
        model_height = input_attrs[0].dims[1];
        model_width = input_attrs[0].dims[2];
        model_channel = input_attrs[0].dims[3];
    }
    if (log_details) {
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

void fill_app_ctx_from_info(rknn_app_context_t* app_ctx, const ModelInfo& info)
{
    app_ctx->io_num = info.io_num;
    app_ctx->input_attrs = nullptr;
    app_ctx->output_attrs = nullptr;

    if (!info.input_attrs.empty()) {
        const size_t bytes = info.input_attrs.size() * sizeof(rknn_tensor_attr);
        app_ctx->input_attrs = static_cast<rknn_tensor_attr*>(malloc(bytes));
        memcpy(app_ctx->input_attrs, info.input_attrs.data(), bytes);
    }
    if (!info.output_attrs.empty()) {
        const size_t bytes = info.output_attrs.size() * sizeof(rknn_tensor_attr);
        app_ctx->output_attrs = static_cast<rknn_tensor_attr*>(malloc(bytes));
        memcpy(app_ctx->output_attrs, info.output_attrs.data(), bytes);
    }

    app_ctx->model_channel = info.model_channel;
    app_ctx->model_width = info.model_width;
    app_ctx->model_height = info.model_height;
    app_ctx->is_quant = info.is_quant;
}

void reset_shared_model(SharedModelCache* cache)
{
    if (!cache) return;
    if (cache->base_ctx != 0) {
        rknn_destroy(cache->base_ctx);
        cache->base_ctx = 0;
    }
    cache->model_path.clear();
    cache->info = ModelInfo();
    cache->ref_count = 0;
}

int ensure_shared_model(const char* model_path, SharedModelCache* cache)
{
    if (!cache->share_enabled) return -1;
    if (cache->base_ctx != 0) {
        if (cache->model_path == model_path) {
            return 0;
        }
        if (cache->ref_count != 0) {
            LOGE("shared model already in use with different model path\n");
            return -1;
        }
        reset_shared_model(cache);
    }

    rknn_context ctx = 0;
    if (init_context_from_file(model_path, &ctx) != 0) {
        return -1;
    }
    ModelInfo info;
    if (query_model_info(ctx, &info, true) != 0) {
        rknn_destroy(ctx);
        return -1;
    }

    cache->model_path = model_path;
    cache->base_ctx = ctx;
    cache->info = std::move(info);
    return 0;
}

int init_context_standalone(const char* model_path, rknn_app_context_t* app_ctx)
{
    rknn_context ctx = 0;
    if (init_context_from_file(model_path, &ctx) != 0) {
        return -1;
    }

    ModelInfo info;
    if (query_model_info(ctx, &info, true) != 0) {
        rknn_destroy(ctx);
        return -1;
    }

    app_ctx->rknn_ctx = ctx;
    fill_app_ctx_from_info(app_ctx, info);
    return 0;
}

}  // namespace

int init_infer_context(const char* model_path, rknn_app_context_t* app_ctx)
{
    if (!model_path || !app_ctx) return -1;

    app_ctx->shared_handle = nullptr;
    app_ctx->rknn_ctx = 0;
    app_ctx->input_attrs = nullptr;
    app_ctx->output_attrs = nullptr;
    app_ctx->is_quant = false;

    {
        std::lock_guard<std::mutex> lk(g_shared_model_mtx);
        if (g_shared_model.share_enabled &&
            ensure_shared_model(model_path, &g_shared_model) == 0) {
            rknn_context ctx = 0;
            const int ret = rknn_dup_context(&g_shared_model.base_ctx, &ctx);
            if (ret == RKNN_SUCC) {
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
            if (g_shared_model.ref_count == 0) {
                reset_shared_model(&g_shared_model);
            }
        }
    }

    return init_context_standalone(model_path, app_ctx);
}

int release_infer_context(rknn_app_context_t* app_ctx)
{
    if (!app_ctx) return -1;

    if (app_ctx->input_attrs != NULL) {
        free(app_ctx->input_attrs);
        app_ctx->input_attrs = NULL;
    }
    if (app_ctx->output_attrs != NULL) {
        free(app_ctx->output_attrs);
        app_ctx->output_attrs = NULL;
    }

    if (app_ctx->rknn_ctx != 0) {
        const int ret = rknn_destroy(app_ctx->rknn_ctx);
        if (ret != RKNN_SUCC) {
            LOGE("rknn_destroy fail! ret=%d\n", ret);
            return -1;
        }
        app_ctx->rknn_ctx = 0;
    }

    if (app_ctx->shared_handle != nullptr) {
        std::lock_guard<std::mutex> lk(g_shared_model_mtx);
        auto* cache = static_cast<SharedModelCache*>(app_ctx->shared_handle);
        if (cache->ref_count > 0) {
            cache->ref_count--;
        }
        if (cache->ref_count == 0 && cache->base_ctx != 0) {
            reset_shared_model(cache);
        }
        app_ctx->shared_handle = nullptr;
    }

    return 0;
}
