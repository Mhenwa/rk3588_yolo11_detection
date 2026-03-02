#ifndef MODULES_DECODE_DECODE_NODE_H_
#define MODULES_DECODE_DECODE_NODE_H_

#include <memory>
#include <vector>

#include <opencv2/opencv.hpp>

#include "core/types/vision_types.h"

namespace modules {
namespace decode {

enum class DecodeBackend {
    kAuto = 0,
    kSoftware,
    kRkMpp,
};

class IFrameDecoder {
public:
    virtual ~IFrameDecoder() = default;
    virtual const char* Name() const = 0;
    virtual bool Decode(const core::types::SourceFrame& input, cv::Mat* out) = 0;
};

struct DecodeNodeOptions {
    DecodeBackend backend = DecodeBackend::kAuto;
};

class DecodeNode {
public:
    DecodeNode();
    explicit DecodeNode(const DecodeNodeOptions& options);

    void RegisterDecoder(std::unique_ptr<IFrameDecoder> decoder);
    bool Decode(const core::types::SourceFrame& input, cv::Mat* out);

private:
    void InitDefaultDecoders();

    DecodeNodeOptions options_;
    std::vector<std::unique_ptr<IFrameDecoder>> decoders_;
};

}  // namespace decode
}  // namespace modules

#endif  // MODULES_DECODE_DECODE_NODE_H_
