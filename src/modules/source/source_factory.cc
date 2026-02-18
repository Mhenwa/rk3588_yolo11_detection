#include "modules/source/source_factory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "modules/source/file_source.h"
#include "modules/source/mipi_source.h"
#include "modules/source/rtsp_source.h"
#include "modules/source/usb_cam_source.h"

namespace
{

    bool starts_with(const std::string &value, const std::string &prefix)
    {
        return value.size() >= prefix.size() &&
               value.compare(0, prefix.size(), prefix) == 0;
    }

    std::string to_lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch)
                       {
                           return static_cast<char>(std::tolower(ch));
                       });
        return value;
    }

    bool is_number_str(const char *s)
    {
        if (!s || *s == '\0')
            return false;
        for (const char *p = s; *p; ++p)
        {
            if (!std::isdigit(static_cast<unsigned char>(*p)))
            {
                return false;
            }
        }
        return true;
    }

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

        bool IsRtspInput(const std::string &input)
        {
            std::string lower = to_lower(input);
            return starts_with(lower, "rtsp://") || starts_with(lower, "rtsps://");
        }

        bool IsMipiSource(const SourceConfig &source)
        {
            std::string lower_name = to_lower(source.name);
            std::string lower_input = to_lower(source.input);
            return starts_with(lower_name, "mipi.") ||
                   lower_input.find("mipi") != std::string::npos ||
                   lower_input.find("rkisp") != std::string::npos;
        }

        std::unique_ptr<SourceBase> BuildSource(const SourceConfig &source)
        {
            const bool rtsp = (source.type == INPUT_RTSP) || IsRtspInput(source.input);
            if (rtsp)
            {
                return std::make_unique<RtspSource>(source.input, source.format);
            }

            if (source.type == INPUT_VIDEO)
            {
                return std::make_unique<FileSource>(source.input);
            }

            const std::string device = normalize_camera_input(source.input);
            if (IsMipiSource(source))
            {
                return std::make_unique<MipiSource>(
                    device, source.width, source.height, source.buffers,
                    source.fps, source.format);
            }
            return std::make_unique<UsbCamSource>(
                device, source.width, source.height, source.buffers,
                source.fps, source.format);
        }

    } // namespace source
} // namespace modules
