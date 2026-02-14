#include "app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {
using json = nlohmann::json;

bool set_error(std::string* error, const std::string& message)
{
    if (error) {
        *error = message;
    }
    return false;
}

std::string to_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool read_required_string(const json& obj,
                          const char* key,
                          std::string* out,
                          const std::string& context,
                          std::string* error)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        return set_error(error, "config missing " + context + "." + key);
    }
    if (!it->is_string()) {
        return set_error(error, "config " + context + "." + key + " must be string");
    }
    if (out) *out = it->get<std::string>();
    if (out && out->empty()) {
        return set_error(error, "config " + context + "." + key + " must be non-empty");
    }
    return true;
}

bool read_string_optional(const json& obj,
                          const char* key,
                          std::string* out,
                          bool* out_set,
                          const std::string& context,
                          std::string* error)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (out_set) *out_set = false;
        return true;
    }
    if (!it->is_string()) {
        return set_error(error, "config " + context + "." + key + " must be string");
    }
    if (out) *out = it->get<std::string>();
    if (out_set) *out_set = true;
    return true;
}

bool read_int_optional(const json& obj,
                       const char* key,
                       int* out,
                       bool* out_set,
                       const std::string& context,
                       std::string* error)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (out_set) *out_set = false;
        return true;
    }
    if (!it->is_number_integer()) {
        return set_error(error, "config " + context + "." + key + " must be integer");
    }
    if (out) *out = it->get<int>();
    if (out_set) *out_set = true;
    return true;
}

bool read_double_optional(const json& obj,
                          const char* key,
                          double* out,
                          bool* out_set,
                          const std::string& context,
                          std::string* error)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (out_set) *out_set = false;
        return true;
    }
    if (!it->is_number()) {
        return set_error(error, "config " + context + "." + key + " must be number");
    }
    if (out) *out = it->get<double>();
    if (out_set) *out_set = true;
    return true;
}

bool parse_dimensions(const json& obj,
                      const std::string& context,
                      int* width,
                      bool* width_set,
                      int* height,
                      bool* height_set,
                      std::string* error)
{
    bool w_set = obj.contains("width");
    bool h_set = obj.contains("height");
    if (w_set != h_set) {
        return set_error(error, "config requires both width and height in " + context);
    }
    if (w_set) {
        const json& w_val = obj.at("width");
        const json& h_val = obj.at("height");
        if (!w_val.is_number_integer() || !h_val.is_number_integer()) {
            return set_error(error, "width/height must be integers in " + context);
        }
        int w = w_val.get<int>();
        int h = h_val.get<int>();
        if (w <= 0 || h <= 0) {
            return set_error(error, "width/height must be > 0 in " + context);
        }
        if (width) *width = w;
        if (height) *height = h;
    }
    if (width_set) *width_set = w_set;
    if (height_set) *height_set = h_set;
    return true;
}

bool parse_buffers(const json& obj,
                   const std::string& context,
                   int* buffers,
                   bool* buffers_set,
                   std::string* error)
{
    bool set = obj.contains("buffers");
    if (set) {
        const json& value = obj.at("buffers");
        if (!value.is_number_integer()) {
            return set_error(error, "buffers must be integer in " + context);
        }
        int count = value.get<int>();
        if (count < 1) {
            return set_error(error, "buffers must be >= 1 in " + context);
        }
        if (buffers) *buffers = count;
    }
    if (buffers_set) *buffers_set = set;
    return true;
}

bool parse_fps(const json& obj,
               const std::string& context,
               double* fps,
               bool* fps_set,
               std::string* error)
{
    bool set = obj.contains("fps");
    if (set) {
        const json& value = obj.at("fps");
        if (!value.is_number()) {
            return set_error(error, "fps must be number in " + context);
        }
        double target = value.get<double>();
        if (target <= 0.0) {
            return set_error(error, "fps must be > 0 in " + context);
        }
        if (fps) *fps = target;
    }
    if (fps_set) *fps_set = set;
    return true;
}

bool parse_format(const json& obj,
                  const std::string& context,
                  std::string* format,
                  bool* format_set,
                  std::string* error)
{
    std::string value;
    bool set = false;
    if (!read_string_optional(obj, "format", &value, &set, context, error)) {
        return false;
    }
    if (set) {
        if (value.empty()) {
            return set_error(error, "format must be non-empty in " + context);
        }
        value = to_lower(value);
        if (value == "mjpg") value = "mjpeg";
        if (value != "auto" && value != "mjpeg" &&
            value != "yuyv" && value != "nv12") {
            return set_error(error, "format must be auto/mjpeg/yuyv/nv12 in " + context);
        }
        if (format) *format = value;
    }
    if (format_set) *format_set = set;
    return true;
}

bool parse_mode_type(const std::string& raw,
                     InputType* out,
                     std::string* error)
{
    if (!out) {
        return set_error(error, "internal error: mode output missing");
    }
    std::string mode = to_lower(raw);
    if (!mode.empty() && mode.rfind("--", 0) == 0) {
        mode = mode.substr(2);
    }
    if (mode == "image") {
        *out = INPUT_IMAGE;
        return true;
    }
    if (mode == "video_camera") {
        *out = INPUT_VIDEO_CAMERA;
        return true;
    }
    return set_error(error, "invalid mode in general.mode: " + raw);
}
}  // namespace

bool parse_config(const nlohmann::json& root,
                  AppConfig* cfg,
                  std::string* error)
{
    if (!cfg) {
        return set_error(error, "config output is null");
    }
    *cfg = AppConfig();

    if (!root.is_object()) {
        return set_error(error, "config root must be object");
    }

    const json* general = nullptr;
    auto general_it = root.find("general");
    if (general_it == root.end() || !general_it->is_object()) {
        return set_error(error, "config missing general");
    }
    general = &(*general_it);

    if (!read_required_string(*general, "model_path",
                              &cfg->model_path, "general", error)) {
        return false;
    }
    std::string mode_value;
    if (!read_required_string(*general, "mode",
                              &mode_value, "general", error)) {
        return false;
    }
    if (!parse_mode_type(mode_value, &cfg->mode_type, error)) {
        return false;
    }
    if (!read_required_string(*general, "label",
                              &cfg->label_path, "general", error)) {
        return false;
    }

    const json* modes = nullptr;
    auto modes_it = root.find("modes");
    if (modes_it == root.end() || !modes_it->is_object()) {
        return set_error(error, "config missing modes");
    }
    modes = &(*modes_it);

    if (cfg->mode_type == INPUT_VIDEO_CAMERA) {
        auto vc_it = modes->find("video_camera");
        if (vc_it == modes->end() || !vc_it->is_object()) {
            return set_error(error, "config missing modes.video_camera");
        }
        const json& vc_cfg = *vc_it;
        auto sources_it = vc_cfg.find("sources");
        if (sources_it == vc_cfg.end() || !sources_it->is_array()) {
            return set_error(error, "config missing modes.video_camera.sources");
        }
        if (sources_it->empty()) {
            return set_error(error, "config missing modes.video_camera.sources");
        }
        for (size_t i = 0; i < sources_it->size(); ++i) {
            const json& source_json = sources_it->at(i);
            if (!source_json.is_object()) {
                return set_error(error, "config modes.video_camera.sources[" +
                                        std::to_string(i) + "] must be object");
            }
            std::string source_context =
                "modes.video_camera.sources[" + std::to_string(i) + "]";
            SourceConfig source;
            if (!read_required_string(source_json, "name",
                                      &source.name, source_context, error)) {
                return false;
            }

            std::string type_value;
            auto type_it = source_json.find("type");
            if (type_it != source_json.end()) {
                if (!type_it->is_string()) {
                    return set_error(error, "config " + source_context + ".type must be string");
                }
                type_value = to_lower(type_it->get<std::string>());
            }
            if (type_value.empty()) {
                std::string lower = to_lower(source.name);
                if (lower.rfind("video.", 0) == 0) {
                    type_value = "video";
                } else if (lower.rfind("camera.", 0) == 0) {
                    type_value = "camera";
                } else {
                    return set_error(error, "invalid source type in " + source_context);
                }
            }
            if (type_value == "video") {
                source.type = INPUT_VIDEO;
            } else if (type_value == "camera") {
                source.type = INPUT_CAMERA;
            } else {
                return set_error(error, "invalid source type in " + source_context +
                                        ": " + type_value);
            }

            if (!read_required_string(source_json, "input",
                                      &source.input, source_context, error)) {
                return false;
            }
            if (!read_int_optional(source_json, "threads", &source.threads,
                                   &source.threads_set, source_context, error)) {
                return false;
            }
            if (!parse_dimensions(source_json, source_context, &source.width,
                                  &source.width_set, &source.height,
                                  &source.height_set, error)) {
                return false;
            }
            if (!parse_buffers(source_json, source_context, &source.buffers,
                               &source.buffers_set, error)) {
                return false;
            }
            if (!parse_fps(source_json, source_context, &source.fps,
                           &source.fps_set, error)) {
                return false;
            }
            if (!parse_format(source_json, source_context, &source.format,
                              &source.format_set, error)) {
                return false;
            }
            if (!read_double_optional(source_json, "conf_threshold",
                                      &source.conf_threshold,
                                      &source.conf_threshold_set,
                                      source_context, error)) {
                return false;
            }
            if (source.conf_threshold < 0.0 || source.conf_threshold > 1.0) {
                return set_error(error, "conf_threshold must be within [0, 1] in " +
                                        source_context);
            }
            if (source.threads < 1) {
                return set_error(error, "threads must be >= 1 for " + source_context);
            }
            cfg->sources.push_back(source);
        }
        return true;
    }

    auto mode_it = modes->find("image");
    if (mode_it == modes->end() || !mode_it->is_object()) {
        return set_error(error, "config missing modes.image");
    }
    const json& mode_cfg = *mode_it;
    if (!read_required_string(mode_cfg, "input", &cfg->input,
                              "modes.image", error)) {
        return false;
    }

    return true;
}

bool load_config(const std::string& path,
                 AppConfig* cfg,
                 std::string* error)
{
    if (!cfg) {
        return set_error(error, "config output is null");
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return set_error(error, "load config failed: " + path + " (cannot open)");
    }

    json root;
    try {
        input >> root;
    } catch (const json::parse_error& err) {
        return set_error(error, "load config failed: " + path +
                                " (parse error: " + err.what() + ")");
    }

    return parse_config(root, cfg, error);
}
