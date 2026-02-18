#ifndef MODULES_SOURCE_RTSP_SOURCE_H_
#define MODULES_SOURCE_RTSP_SOURCE_H_

#include <string>

#include <opencv2/opencv.hpp>

#include "modules/source/source_base.h"

namespace modules {
namespace source {

class RtspSource : public SourceBase {
public:
    RtspSource(std::string url, std::string codec);

    bool Open() override;
    void Close() override;
    bool Read(SourceFrame* out) override;

private:
    bool OpenCaptureWithCodec(const std::string& url,
                              const std::string& codec);

    std::string url_;
    std::string codec_;
    cv::VideoCapture capture_;
};

}  // namespace source
}  // namespace modules

#endif  // MODULES_SOURCE_RTSP_SOURCE_H_
