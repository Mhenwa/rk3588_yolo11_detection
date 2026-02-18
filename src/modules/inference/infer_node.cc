#include "modules/inference/infer_node.h"

#include <chrono>
#include <cstring>

#include "core/log/app_log.h"

namespace modules
{
    namespace inference
    {

        namespace
        {
            int run_forward(rknn_app_context_t *app_ctx,
                            image_buffer_t *preprocessed_img,
                            std::vector<rknn_output> *outputs)
            {
                if (!app_ctx || !preprocessed_img || !outputs)
                {
                    return -1;
                }

                rknn_input inputs[app_ctx->io_num.n_input];
                memset(inputs, 0, sizeof(inputs));

                inputs[0].index = 0;
                inputs[0].type = RKNN_TENSOR_UINT8;
                inputs[0].fmt = RKNN_TENSOR_NHWC;
                inputs[0].size =
                    app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;
                inputs[0].buf = preprocessed_img->virt_addr;

                int ret = rknn_inputs_set(app_ctx->rknn_ctx, app_ctx->io_num.n_input, inputs);
                if (ret < 0)
                {
                    LOGE("rknn_input_set fail! ret=%d\n", ret);
                    return -1;
                }

                ret = rknn_run(app_ctx->rknn_ctx, nullptr);
                if (ret < 0)
                {
                    LOGE("rknn_run fail! ret=%d\n", ret);
                    return -1;
                }

                outputs->assign(app_ctx->io_num.n_output, rknn_output{});
                for (int i = 0; i < app_ctx->io_num.n_output; i++)
                {
                    (*outputs)[i].index = i;
                    (*outputs)[i].want_float = (!app_ctx->is_quant);
                }

                ret = rknn_outputs_get(app_ctx->rknn_ctx,
                                       app_ctx->io_num.n_output,
                                       outputs->data(),
                                       NULL);
                if (ret < 0)
                {
                    LOGE("rknn_outputs_get fail! ret=%d\n", ret);
                    outputs->clear();
                    return -1;
                }
                return 0;
            }
        } // namespace

        bool InferNode::Run(rknn_app_context_t *app_ctx,
                            const image_buffer_t &input_image,
                            InferOutput *out) const
        {
            if (!app_ctx || !out)
                return false;

            out->raw_outputs.clear();
            out->infer_ms = 0.0;

            image_buffer_t forward_input = input_image;
            auto t0 = std::chrono::steady_clock::now();
            int ret = run_forward(app_ctx, &forward_input, &out->raw_outputs);
            auto t1 = std::chrono::steady_clock::now();
            out->infer_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (ret != 0)
            {
                LOGE("run_forward failed: %d\n", ret);
                return false;
            }
            return true;
        }

    } // namespace inference
} // namespace modules
