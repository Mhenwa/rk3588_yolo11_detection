#include "modules/source/rtsp_source.h"

#include <cstdio>
#include <utility>
#include <vector>

#include "core/log/app_log.h"
#include "core/utils/string_utils.h"

namespace {
constexpr int kRtspLatencyMs = 200;

std::string normalize_codec(const std::string& codec)
{
    if (codec.empty()) return "auto";
    std::string lower = core::utils::to_lower(codec);
    if (lower == "mjpg") lower = "mjpeg";
    if (lower == "h264" || lower == "h265" || lower == "auto") {
        return lower;
    }
    return "auto";
}

std::string escape_gst_string(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string build_rtsp_mpp_pipeline(const std::string& url,
                                    const std::string& codec)
{
    const std::string depay = (codec == "h265") ? "rtph265depay" : "rtph264depay";
    const std::string parser = (codec == "h265") ? "h265parse" : "h264parse";
    const std::string escaped_url = escape_gst_string(url);

    char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "rtspsrc location=\"%s\" protocols=tcp latency=%d ! "
             "%s ! %s ! mppvideodec ! videoconvert ! "
             "video/x-raw,format=BGR ! appsink sync=false drop=true max-buffers=1",
             escaped_url.c_str(), kRtspLatencyMs,
             depay.c_str(), parser.c_str());
    return std::string(pipeline);
}

std::string build_rtsp_decodebin_pipeline(const std::string& url)
{
    const std::string escaped_url = escape_gst_string(url);
    char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "uridecodebin uri=\"%s\" ! videoconvert ! "
             "video/x-raw,format=BGR ! appsink sync=false drop=true max-buffers=1",
             escaped_url.c_str());
    return std::string(pipeline);
}

bool try_open_capture(cv::VideoCapture* cap,
                      const std::string& source,
                      int backend,
                      const char* desc)
{
    cap->release();
    if (!cap->open(source, backend) || !cap->isOpened()) {
        return false;
    }
    LOGI("RTSP opened via %s\n", desc);
    return true;
}
}  // namespace

namespace modules {
namespace source {

RtspSource::RtspSource(std::string url, std::string codec)
    : url_(std::move(url)), codec_(std::move(codec))
{
}

bool RtspSource::Open()
{
    return OpenCaptureWithCodec(url_, codec_);
}

void RtspSource::Close()
{
    capture_.release();
}

bool RtspSource::Read(SourceFrame* out)
{
    if (!out) return false;
    out->capture_tp = std::chrono::steady_clock::now();
    if (!capture_.read(out->frame)) {
        return false;
    }
    out->data.clear();
    out->format = SourceFrameFormat::kBgr;
    out->width = out->frame.cols;
    out->height = out->frame.rows;
    return !out->frame.empty();
}

bool RtspSource::OpenCaptureWithCodec(const std::string& url,
                                      const std::string& codec)
{
    const std::string codec_mode = normalize_codec(codec);
    std::vector<std::pair<std::string, std::string>> gst_attempts;

    if (codec_mode == "h264") {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h264"),
                                  "gstreamer rtsp h264 mpp");
    } else if (codec_mode == "h265") {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h265"),
                                  "gstreamer rtsp h265 mpp");
    } else {
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h264"),
                                  "gstreamer rtsp h264 mpp");
        gst_attempts.emplace_back(build_rtsp_mpp_pipeline(url, "h265"),
                                  "gstreamer rtsp h265 mpp");
    }
    gst_attempts.emplace_back(build_rtsp_decodebin_pipeline(url),
                              "gstreamer uridecodebin");

    for (const auto& item : gst_attempts) {
        if (try_open_capture(&capture_, item.first, cv::CAP_GSTREAMER,
                             item.second.c_str())) {
            return true;
        }
    }

    if (try_open_capture(&capture_, url, cv::CAP_GSTREAMER,
                         "gstreamer direct rtsp")) {
        return true;
    }
    if (try_open_capture(&capture_, url, cv::CAP_ANY,
                         "opencv default backend")) {
        return true;
    }

    capture_.release();
    return false;
}

}  // namespace source
}  // namespace modules
