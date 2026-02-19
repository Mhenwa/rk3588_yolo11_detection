#include "app_config.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool condition, const char *message)
{
  if (!condition)
  {
    std::fprintf(stderr, "FAIL: %s\n", message);
    g_failures++;
  }
}

static nlohmann::json parse_json(const char *text)
{
  return nlohmann::json::parse(text);
}

int main()
{
  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "video.0",
                  "type": "video",
                  "input": "video.mp4"
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(ok, "parse video_camera minimal config");
    check(cfg.mode_type == INPUT_VIDEO_CAMERA, "video_camera mode type");
    check(cfg.sources.size() == 1, "video source exists");
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "camera.0",
                  "type": "usb_camera",
                  "input": "/dev/video0",
                  "format": "mjpg"
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(ok, "parse video_camera config");
    check(cfg.mode_type == INPUT_VIDEO_CAMERA, "video_camera mode type");
    check(cfg.sources.size() == 1, "one source parsed");
    if (!cfg.sources.empty())
    {
      const SourceConfig &src = cfg.sources.front();
      check(src.type == INPUT_USB_CAMERA, "source type parsed as usb_camera");
      check(src.format == "mjpeg", "format normalized to mjpeg");
      check(!src.conf_threshold_set, "conf_threshold unset");
      check(src.conf_threshold == kDefaultConfThreshold, "conf_threshold default");
      check(!src.threads_set, "threads unset");
      check(src.threads == 3, "threads default");
    }
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "camera.0",
                  "type": "usb_camera",
                  "input": "/dev/video0",
                  "conf_threshold": 1.5
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(!ok, "reject conf_threshold > 1");
    check(error.find("conf_threshold") != std::string::npos,
          "conf_threshold error text");
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "camera.0",
                  "type": "usb_camera",
                  "input": "/dev/video0",
                  "width": 640
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(!ok, "reject width without height");
    check(error.find("width") != std::string::npos,
          "width/height error text");
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "rtsp.0",
                  "type": "rtsp",
                  "input": "rtsp://127.0.0.1:8554/test",
                  "format": "h264"
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(ok, "parse rtsp config");
    check(cfg.sources.size() == 1, "one rtsp source parsed");
    if (!cfg.sources.empty())
    {
      const SourceConfig &src = cfg.sources.front();
      check(src.type == INPUT_RTSP, "source type parsed as rtsp");
      check(src.format == "h264", "rtsp codec parsed");
    }
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "rtsp.0",
                  "type": "rtsp",
                  "input": "/dev/video0",
                  "format": "h264"
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(ok, "allow any input string for rtsp type");
    check(cfg.sources.size() == 1, "one source parsed");
    if (!cfg.sources.empty())
    {
      check(cfg.sources.front().type == INPUT_RTSP, "rtsp type kept");
    }
  }

  {
    const char *text = R"JSON(
        {
          "general": {
            "mode": "video_camera",
            "label": "labels.txt",
            "model_path": "model.rknn"
          },
          "modes": {
            "video_camera": {
              "sources": [
                {
                  "name": "camera.0",
                  "type": "camera",
                  "input": "/dev/video0"
                }
              ]
            }
          }
        }
        )JSON";
    nlohmann::json root = parse_json(text);
    AppConfig cfg;
    std::string error;
    bool ok = parse_config(root, &cfg, &error);
    check(!ok, "reject legacy camera type");
    check(error.find("video|rtsp|mipi_camera|usb_camera") != std::string::npos,
          "type enum error text");
  }

  if (g_failures == 0)
  {
    std::printf("All app_config tests passed.\n");
  }
  return g_failures == 0 ? 0 : 1;
}
