#include "modules/source/source_factory.h"

#include "modules/source/file_source.h"
#include "modules/source/mipi_source.h"
#include "modules/source/rtsp_source.h"
#include "modules/source/usb_cam_source.h"



namespace modules
{
    namespace source
    {

        std::unique_ptr<SourceBase> BuildSource(const SourceConfig &source)
        {
            if (source.type == INPUT_RTSP)
            {
                return std::make_unique<RtspSource>(source.input, source.format);
            }

            if (source.type == INPUT_VIDEO)
            {
                return std::make_unique<FileSource>(source.input);
            }

            if (source.type == INPUT_MIPI_CAMERA)
            {
                return std::make_unique<MipiSource>(
                    source.input, source.width, source.height, source.fps,
                    source.format);
            }

            if (source.type == INPUT_USB_CAMERA)
            {
                return std::make_unique<UsbCamSource>(
                    source.input, source.width, source.height, source.fps,
                    source.format);
            }
            return nullptr;
        }

    } // namespace source
} // namespace modules
