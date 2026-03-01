#include "modules/preprocess/preprocess_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "core/log/app_log.h"
#include "core/utils/rga_debug_gate.h"

namespace modules
{
    namespace preprocess
    {
        namespace
        {
            constexpr size_t kGuardBytes = 4096;
            constexpr unsigned char kGuardPattern = 0x5A;

            bool check_guard(const std::vector<unsigned char> &storage,
                             size_t guard_bytes,
                             size_t payload_bytes)
            {
                if (guard_bytes == 0 || storage.size() < payload_bytes + guard_bytes * 2)
                {
                    return true;
                }
                const unsigned char *base = storage.data();
                for (size_t i = 0; i < guard_bytes; ++i)
                {
                    if (base[i] != kGuardPattern)
                        return false;
                }
                const unsigned char *tail = base + guard_bytes + payload_bytes;
                for (size_t i = 0; i < guard_bytes; ++i)
                {
                    if (tail[i] != kGuardPattern)
                        return false;
                }
                return true;
            }
        } // namespace

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

            const size_t payload_bytes =
                static_cast<size_t>(app_ctx.model_width) *
                static_cast<size_t>(app_ctx.model_height) *
                static_cast<size_t>(app_ctx.model_channel);
            const size_t guard_bytes = rga_debug_guard_check_enabled() ? kGuardBytes : 0;
            out->input_storage.resize(payload_bytes + guard_bytes * 2);
            // if (guard_bytes > 0)
            // {
            //     std::fill(out->input_storage.begin(),
            //               out->input_storage.begin() + static_cast<std::ptrdiff_t>(guard_bytes),
            //               kGuardPattern);
            //     std::fill(out->input_storage.end() - static_cast<std::ptrdiff_t>(guard_bytes),
            //               out->input_storage.end(),
            //               kGuardPattern);
            // }

            image_buffer_t dst;
            memset(&dst, 0, sizeof(dst));
            dst.width = app_ctx.model_width;
            dst.height = app_ctx.model_height;
            dst.format = IMAGE_FORMAT_RGB888;
            dst.size = static_cast<int>(payload_bytes);
            dst.virt_addr = out->input_storage.data() + guard_bytes;

            if (convert_image_with_letterbox(&src, &dst, &out->letterbox, 114) != 0)
            {
                LOGE("preprocess letterbox failed\n");
                return false;
            }
            // if (!check_guard(out->input_storage, guard_bytes, payload_bytes))
            // {
            //     LOGE("preprocess RGA/resize overflow detected (guard corrupted)\n");
            //     abort();
            // }

            out->input_image = dst;
            return true;
        }

    } // namespace preprocess
} // namespace modules
