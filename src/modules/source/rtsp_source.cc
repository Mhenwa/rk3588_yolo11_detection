#include "modules/source/rtsp_source.h"

#include <utility>

namespace modules
{
    namespace source
    {

        RtspSource::RtspSource(std::string url, std::string codec)
            : url_(std::move(url)), codec_(std::move(codec))
        {
        }

        bool RtspSource::Open()
        {
            capture_.release();
            return OpenCaptureWithCodec(url_, codec_);
        }

        void RtspSource::Close()
        {
            capture_.release();
        }

        bool RtspSource::Read(SourceFrame *out)
        {
            (void)out;
            return false;
        }

        bool RtspSource::OpenCaptureWithCodec(const std::string &url,
                                              const std::string &codec)
        {
            (void)url;
            (void)codec;
            return false;
        }

    } // namespace source
} // namespace modules
