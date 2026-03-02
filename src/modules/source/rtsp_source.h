#ifndef MODULES_SOURCE_RTSP_SOURCE_H_
#define MODULES_SOURCE_RTSP_SOURCE_H_

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "modules/source/source_base.h"

class Capturer;

namespace modules {
namespace source {

typedef struct {
    char fmt[16];
    int chnId;
    int width;
    int height;
    int horStride;
    int verStride;
    int dataSize;
} RtspImgDesc_t;

int rtspVideoOutHandle(char* imgData, RtspImgDesc_t imgDesc);

class RtspSource : public SourceBase {
public:
    struct RawFrame {
        RtspImgDesc_t desc;
        std::vector<unsigned char> data;
        std::chrono::steady_clock::time_point capture_tp;
    };

    RtspSource(std::string url, std::string codec);
    ~RtspSource() override;

    bool Open() override;
    void Close() override;
    bool Read(SourceFrame* out) override;

private:
    friend int rtspVideoOutHandle(char* imgData, RtspImgDesc_t imgDesc);
    int PushFrame(char* imgData, const RtspImgDesc_t& imgDesc);

    bool OpenCaptureWithCodec(const std::string& url,
                              const std::string& codec);

    std::string url_;
    std::string codec_;
    int channel_id_ = -1;
    bool opened_ = false;
    bool stop_ = false;
    bool first_frame_logged_ = false;
    std::unique_ptr<Capturer> capturer_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<RawFrame> frames_;
};

}  // namespace source
}  // namespace modules

#endif  // MODULES_SOURCE_RTSP_SOURCE_H_
