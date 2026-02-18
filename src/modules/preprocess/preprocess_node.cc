#include "modules/preprocess/preprocess_node.h"

#include <cstring>

#include "core/log/app_log.h"

namespace modules
{
    namespace preprocess
    {

        bool PreprocessNode::Run(const cv::Mat &frame_bgr,
                                 const rknn_app_context_t &app_ctx,
                                 PreprocessOutput *out) const
        {
            if (!out || frame_bgr.empty())
                return false;

            memset(&out->input_image, 0, sizeof(out->input_image));
            memset(&out->letterbox, 0, sizeof(out->letterbox));

            image_buffer_t src;
            memset(&src, 0, sizeof(src));
            src.width = frame_bgr.cols;
            src.height = frame_bgr.rows;
            src.format = IMAGE_FORMAT_BGR888;
            src.virt_addr = frame_bgr.data;

            out->input_storage.resize(
                static_cast<size_t>(app_ctx.model_width) *
                static_cast<size_t>(app_ctx.model_height) *
                static_cast<size_t>(app_ctx.model_channel));

            image_buffer_t dst;
            memset(&dst, 0, sizeof(dst));
            dst.width = app_ctx.model_width;
            dst.height = app_ctx.model_height;
            dst.format = IMAGE_FORMAT_RGB888;
            dst.size = static_cast<int>(out->input_storage.size());
            dst.virt_addr = out->input_storage.data();

            if (convert_image_with_letterbox(&src, &dst, &out->letterbox, 114) != 0)
            {
                LOGE("preprocess letterbox failed\n");
                return false;
            }

            out->input_image = dst;
            return true;
        }

    } // namespace preprocess
} // namespace modules
