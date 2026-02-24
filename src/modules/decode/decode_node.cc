/*
    Todo rkmpp
*/
#include "modules/decode/decode_node.h"

#include <cstddef>
#include <memory>

#include <turbojpeg.h>

#include "core/log/app_log.h"

namespace modules
{
    namespace decode
    {

        namespace
        {

            class SoftwareDecoder : public IFrameDecoder
            {
            public:
                SoftwareDecoder() = default;
                ~SoftwareDecoder() override
                {
                    if (tj_handle_)
                    {
                        tjDestroy(tj_handle_);
                        tj_handle_ = nullptr;
                    }
                }

                const char *Name() const override
                {
                    return "software";
                }

                bool Decode(const core::types::SourceFrame& input, cv::Mat* out) override
                {
                    if (!out)
                        return false;
                    switch (input.format)
                    {
                    case core::types::SourceFrameFormat::kBgr:
                        *out = input.frame;
                        return !out->empty();
                    case core::types::SourceFrameFormat::kMjpeg:
                        return DecodeMjpeg(input, out);
                    case core::types::SourceFrameFormat::kNv12:
                        return DecodeNv12(input, out);
                    case core::types::SourceFrameFormat::kYuyv:
                        return DecodeYuyv(input, out);
                    default:
                        return false;
                    }
                }

            private:
                bool DecodeMjpeg(const core::types::SourceFrame& input, cv::Mat* out)
                {
                    if (input.data.empty())
                        return false;
                    if (!tj_handle_)
                    {
                        tj_handle_ = tjInitDecompress();
                        if (!tj_handle_)
                        {
                            LOGE("tjInitDecompress failed\n");
                            return false;
                        }
                    }

                    int width = 0;
                    int height = 0;
                    int subsamp = 0;
                    int colorspace = 0;
                    if (tjDecompressHeader3(tj_handle_,
                                            input.data.data(),
                                            static_cast<unsigned long>(input.data.size()),
                                            &width,
                                            &height,
                                            &subsamp,
                                            &colorspace) != 0)
                    {
                        LOGE("turbojpeg header decode failed: %s\n", tjGetErrorStr2(tj_handle_));
                        return false;
                    }
                    if (width <= 0 || height <= 0)
                        return false;

                    out->create(height, width, CV_8UC3);
                    const int pitch = width * 3;
                    if (tjDecompress2(tj_handle_,
                                      input.data.data(),
                                      static_cast<unsigned long>(input.data.size()),
                                      out->data,
                                      width,
                                      pitch,
                                      height,
                                      TJPF_BGR,
                                      0) != 0)
                    {
                        LOGE("turbojpeg decode failed: %s\n", tjGetErrorStr2(tj_handle_));
                        out->release();
                        return false;
                    }
                    return true;
                }

                bool DecodeNv12(const core::types::SourceFrame& input, cv::Mat* out)
                {
                    if (input.width <= 0 || input.height <= 0)
                        return false;
                    const size_t required =
                        static_cast<size_t>(input.width) *
                        static_cast<size_t>(input.height) * 3 / 2;
                    if (input.data.size() < required)
                    {
                        LOGE("NV12 buffer too small: %zu < %zu\n", input.data.size(), required);
                        return false;
                    }

                    cv::Mat nv12(input.height * 3 / 2, input.width, CV_8UC1,
                                 const_cast<unsigned char *>(input.data.data()));
                    cv::cvtColor(nv12, *out, cv::COLOR_YUV2BGR_NV12);
                    return !out->empty();
                }

                bool DecodeYuyv(const core::types::SourceFrame& input, cv::Mat* out)
                {
                    if (input.width <= 0 || input.height <= 0)
                        return false;
                    const size_t required =
                        static_cast<size_t>(input.width) *
                        static_cast<size_t>(input.height) * 2;
                    if (input.data.size() < required)
                    {
                        LOGE("YUYV buffer too small: %zu < %zu\n", input.data.size(), required);
                        return false;
                    }

                    cv::Mat yuyv(input.height, input.width, CV_8UC2,
                                 const_cast<unsigned char *>(input.data.data()));
                    cv::cvtColor(yuyv, *out, cv::COLOR_YUV2BGR_YUYV);
                    return !out->empty();
                }

                tjhandle tj_handle_ = nullptr;

            };

            class RkMppDecoder : public IFrameDecoder
            {
            public:
                const char *Name() const override
                {
                    return "rkmpp";
                }

                bool Decode(const core::types::SourceFrame& input, cv::Mat* out) override
                {
                    (void)input;
                    (void)out;
                    if (!logged_)
                    {
                        LOGI("RKMPP decoder backend is not enabled yet, fallback to software\n");
                        logged_ = true;
                    }
                    return false;
                }

            private:
                bool logged_ = false;
            };

        } // namespace

        DecodeNode::DecodeNode()
            : options_()
        {
            InitDefaultDecoders();
        }

        DecodeNode::DecodeNode(const DecodeNodeOptions &options)
            : options_(options)
        {
            InitDefaultDecoders();
        }

        void DecodeNode::RegisterDecoder(std::unique_ptr<IFrameDecoder> decoder)
        {
            if (!decoder)
                return;
            decoders_.push_back(std::move(decoder));
        }

        bool DecodeNode::Decode(const core::types::SourceFrame& input, cv::Mat* out)
        {
            if (!out)
                return false;

            for (auto &decoder : decoders_)
            {
                if (decoder && decoder->Decode(input, out))
                {
                    return true;
                }
            }
            LOGE("no decoder handled frame format=%s\n", core::types::ToString(input.format));
            return false;
        }

        void DecodeNode::InitDefaultDecoders()
        {
            decoders_.clear();

            if (options_.backend == DecodeBackend::kSoftware)
            {
                RegisterDecoder(std::make_unique<SoftwareDecoder>());
                return;
            }

            if (options_.backend == DecodeBackend::kRkMpp ||
                options_.backend == DecodeBackend::kAuto)
            {
                RegisterDecoder(std::make_unique<RkMppDecoder>());
            }

            RegisterDecoder(std::make_unique<SoftwareDecoder>());
        }

    } // namespace decode
} // namespace modules
