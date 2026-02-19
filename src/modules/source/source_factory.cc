#include "modules/source/source_factory.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/utils/app_utils.h"
#include "modules/source/file_source.h"
#include "modules/source/mipi_source.h"
#include "modules/source/rtsp_source.h"
#include "modules/source/usb_cam_source.h"

namespace
{
    std::string normalize_camera_input(const std::string &input)
    {
        if (is_number_str(input.c_str()))
        {
            const int index = std::atoi(input.c_str());
            char dev_path[64];
            snprintf(dev_path, sizeof(dev_path), "/dev/video%d", index);
            return std::string(dev_path);
        }
        return input;
    }

} // namespace

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

            const std::string device = normalize_camera_input(source.input);
            if (source.type == INPUT_MIPI_CAMERA)
            {
                return std::make_unique<MipiSource>(
                    device, source.width, source.height, source.buffers,
                    source.fps, source.format);
            }

            if (source.type == INPUT_USB_CAMERA)
            {
                return std::make_unique<UsbCamSource>(
                    device, source.width, source.height, source.buffers,
                    source.fps, source.format);
            }
            return nullptr;
        }

    } // namespace source
} // namespace modules
