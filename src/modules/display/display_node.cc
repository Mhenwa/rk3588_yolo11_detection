#include "modules/display/display_node.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#if __has_include(<opencv2/freetype.hpp>)
#include <opencv2/freetype.hpp>
#define APP_HAS_OPENCV_FREETYPE 1
#else
#define APP_HAS_OPENCV_FREETYPE 0
#endif

#include "display.h"
#include "core/utils/rga_debug_gate.h"
#include "../compositor/grid_compositor.h"
#include "core/log/app_log.h"

namespace
{
    constexpr int kCvWaitKeyDelayMs = 1;
    constexpr int kOverlayMarginPx = 10;    // overlay距离窗口边缘
    constexpr int kOverlayGapPx = 8;      // name 和 info 两行之间的间距
    constexpr int kOverlayPadXPx = 12;     // 黑框左右内边距
    constexpr int kOverlayPadYPx = 12;      // 黑框上下内边距
    constexpr unsigned char kOverlayBgAlpha = 112;    // 黑框透明度

    constexpr int kNameFontHeight = 34;    // 第一行文字（source name）字号
    constexpr double kNameFallbackScale = 0.8;
    constexpr int kNameFallbackThickness = 1;

    constexpr int kInfoFontHeight = 28;    // 第二行文字（FPS/Infer）字号
    constexpr double kInfoFallbackScale = 0.7;
    constexpr int kInfoFallbackThickness = 1;
    constexpr int kInfoRefreshIntervalMs = 250;

    std::string NormalizeWindowName(const std::string &window_name)
    {
        if (window_name.empty())
        {
            return "dock_blindspot";
        }
        return window_name;
    }

    std::vector<std::string> SplitLines(const std::string &text)
    {
        std::vector<std::string> lines;
        if (text.empty())
        {
            return lines;
        }
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty())
            {
                lines.push_back(line);
            }
        }
        if (lines.empty())
        {
            lines.push_back(text);
        }
        return lines;
    }

    bool FileExists(const std::string &path)
    {
        std::ifstream in(path);
        return in.good();
    }

    struct OverlayTextState
    {
        std::once_flag init_once;
        std::mutex draw_mutex;
        bool ready = false;
        std::string font_path;
#if APP_HAS_OPENCV_FREETYPE
        cv::Ptr<cv::freetype::FreeType2> ft2;
#endif
    };

    OverlayTextState &TextState()
    {
        static OverlayTextState state;
        return state;
    }

    struct OverlayPatch
    {
        cv::Mat bgr;
        cv::Mat alpha;

        bool IsValid() const
        {
            return !bgr.empty() && !alpha.empty();
        }
    };

    using OverlayPatchPtr = std::shared_ptr<OverlayPatch>;

    struct DynamicOverlayEntry
    {
        std::string text;
        std::chrono::steady_clock::time_point last_refresh =
            std::chrono::steady_clock::time_point::min();
        OverlayPatchPtr patch;
    };

    struct OverlayCacheState
    {
        std::mutex mutex;
        std::unordered_map<std::string, OverlayPatchPtr> name_patches;
        std::unordered_map<std::string, DynamicOverlayEntry> info_patches;
    };

    OverlayCacheState &PatchState()
    {
        static OverlayCacheState state;
        return state;
    }

    void InitOverlayTextRenderer()
    {
        OverlayTextState &state = TextState();
        std::call_once(state.init_once, [&state]()
                       {
#if APP_HAS_OPENCV_FREETYPE
                           std::vector<std::string> candidates;
                           const char *font_env = std::getenv("APP_FONT_PATH");
                           if (font_env && *font_env)
                           {
                               candidates.push_back(font_env);
                           }
                           // candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
                           candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc");
                           candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf");
                           candidates.push_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
                           candidates.push_back("/usr/share/fonts/truetype/noto/NotoSansCJKsc-Regular.otf");
                           candidates.push_back("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc");
                           candidates.push_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");

                           std::string selected;
                           for (const auto &candidate : candidates)
                           {
                               if (FileExists(candidate))
                               {
                                   selected = candidate;
                                   break;
                               }
                           }
                           if (selected.empty())
                           {
                               LOGW("no CJK font found, name overlay will fallback to cv::putText");
                               return;
                           }

                           try
                           {
                               state.ft2 = cv::freetype::createFreeType2();
                               state.ft2->loadFontData(selected, 0);
                               state.font_path = selected;
                               state.ready = true;
                               LOGI("overlay text renderer uses font: %s\n", selected.c_str());
                           }
                           catch (const cv::Exception &e)
                           {
                               LOGW("init freetype failed, fallback to cv::putText: %s\n", e.what());
                           }
#else
                           LOGW("opencv freetype module is unavailable, Chinese overlay is disabled");
#endif
                       });
    }

    cv::Size MeasureOverlayText(const std::string &text,
                                int font_height,
                                double fallback_scale,
                                int fallback_thickness,
                                int *baseline)
    {
        if (baseline)
        {
            *baseline = 0;
        }
        if (text.empty())
        {
            return cv::Size();
        }

        InitOverlayTextRenderer();
        OverlayTextState &state = TextState();

#if APP_HAS_OPENCV_FREETYPE
        if (state.ready && state.ft2)
        {
            std::lock_guard<std::mutex> lk(state.draw_mutex);
            return state.ft2->getTextSize(text, font_height, -1, baseline);
        }
#endif
        return cv::getTextSize(text,
                               cv::FONT_HERSHEY_SIMPLEX,
                               fallback_scale,
                               fallback_thickness,
                               baseline);
    }

    void RenderOverlayText(cv::Mat *frame,
                           const std::string &text,
                           const cv::Point &org,
                           int font_height,
                           double fallback_scale,
                           int fallback_thickness,
                           const cv::Scalar &color)
    {
        if (!frame || frame->empty() || text.empty())
        {
            return;
        }

        InitOverlayTextRenderer();
        OverlayTextState &state = TextState();

#if APP_HAS_OPENCV_FREETYPE
        if (state.ready && state.ft2)
        {
            std::lock_guard<std::mutex> lk(state.draw_mutex);
            state.ft2->putText(*frame,
                               text,
                               org,
                               font_height,
                               color,
                               -1,
                               cv::LINE_AA,
                               false);
            return;
        }
#endif
        cv::putText(*frame,
                    text,
                    org,
                    cv::FONT_HERSHEY_SIMPLEX,
                    fallback_scale,
                    color,
                    fallback_thickness,
                    cv::LINE_AA);
    }

    OverlayPatchPtr BuildOverlayPatch(const std::string &text,
                                      int font_height,
                                      double fallback_scale,
                                      int fallback_thickness)
    {
        if (text.empty())
        {
            return OverlayPatchPtr();
        }

        int baseline = 0;
        const cv::Size text_size = MeasureOverlayText(text,
                                                      font_height,
                                                      fallback_scale,
                                                      fallback_thickness,
                                                      &baseline);
        if (text_size.width <= 0 || text_size.height <= 0)
        {
            return OverlayPatchPtr();
        }

        const int scratch_width = text_size.width + 4 * kOverlayPadXPx;
        const int scratch_height = std::max(text_size.height + std::max(0, baseline) +
                                                4 * kOverlayPadYPx,
                                            font_height + 4 * kOverlayPadYPx);
        if (scratch_width <= 0 || scratch_height <= 0)
        {
            return OverlayPatchPtr();
        }

        cv::Mat text_layer(scratch_height, scratch_width, CV_8UC3, cv::Scalar(0, 0, 0));
        RenderOverlayText(&text_layer,
                          text,
                          cv::Point(2 * kOverlayPadXPx,
                                    kOverlayPadYPx + text_size.height + std::max(0, baseline)),
                          font_height,
                          fallback_scale,
                          fallback_thickness,
                          cv::Scalar(255, 255, 255));

        cv::Mat text_mask;
        cv::cvtColor(text_layer, text_mask, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point> non_zero_points;
        cv::findNonZero(text_mask, non_zero_points);
        if (non_zero_points.empty())
        {
            return OverlayPatchPtr();
        }

        const cv::Rect text_bounds = cv::boundingRect(non_zero_points);
        const int width = text_bounds.width + 2 * kOverlayPadXPx;
        const int height = text_bounds.height + 2 * kOverlayPadYPx;
        if (width <= 0 || height <= 0)
        {
            return OverlayPatchPtr();
        }

        OverlayPatchPtr patch = std::make_shared<OverlayPatch>();
        patch->bgr = cv::Mat(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
        patch->alpha = cv::Mat(height, width, CV_8UC1, cv::Scalar(kOverlayBgAlpha));

        const cv::Rect dst_roi(kOverlayPadXPx, kOverlayPadYPx, text_bounds.width, text_bounds.height);
        text_layer(text_bounds).copyTo(patch->bgr(dst_roi));
        cv::Mat alpha_dst = patch->alpha(dst_roi);
        cv::max(alpha_dst, text_mask(text_bounds), alpha_dst);
        return patch;
    }

    void BlendOverlayPatch(cv::Mat *frame,
                           const OverlayPatchPtr &patch,
                           const cv::Point &origin)
    {
        if (!frame || frame->empty() || frame->type() != CV_8UC3 || !patch ||
            !patch->IsValid())
        {
            return;
        }

        const int dst_x = std::max(origin.x, 0);
        const int dst_y = std::max(origin.y, 0);
        const int src_x = std::max(0, -origin.x);
        const int src_y = std::max(0, -origin.y);
        const int width = std::min(frame->cols - dst_x, patch->bgr.cols - src_x);
        const int height = std::min(frame->rows - dst_y, patch->bgr.rows - src_y);
        if (width <= 0 || height <= 0)
        {
            return;
        }

        cv::Mat dst_roi = (*frame)(cv::Rect(dst_x, dst_y, width, height));
        const cv::Mat src_roi = patch->bgr(cv::Rect(src_x, src_y, width, height));
        const cv::Mat alpha_roi = patch->alpha(cv::Rect(src_x, src_y, width, height));

        for (int y = 0; y < height; ++y)
        {
            unsigned char *dst_row = dst_roi.ptr<unsigned char>(y);
            const unsigned char *src_row = src_roi.ptr<unsigned char>(y);
            const unsigned char *alpha_row = alpha_roi.ptr<unsigned char>(y);
            for (int x = 0; x < width; ++x)
            {
                const int alpha = alpha_row[x];
                if (alpha <= 0)
                {
                    continue;
                }
                const int inv_alpha = 255 - alpha;
                const int idx = x * 3;
                dst_row[idx + 0] =
                    static_cast<unsigned char>((src_row[idx + 0] * alpha +
                                                dst_row[idx + 0] * inv_alpha + 127) /
                                               255);
                dst_row[idx + 1] =
                    static_cast<unsigned char>((src_row[idx + 1] * alpha +
                                                dst_row[idx + 1] * inv_alpha + 127) /
                                               255);
                dst_row[idx + 2] =
                    static_cast<unsigned char>((src_row[idx + 2] * alpha +
                                                dst_row[idx + 2] * inv_alpha + 127) /
                                               255);
            }
        }
    }

    OverlayPatchPtr GetOrCreateNamePatch(const std::string &source_name)
    {
        if (source_name.empty())
        {
            return OverlayPatchPtr();
        }

        OverlayCacheState &state = PatchState();
        std::lock_guard<std::mutex> lk(state.mutex);
        auto it = state.name_patches.find(source_name);
        if (it != state.name_patches.end())
        {
            return it->second;
        }

        OverlayPatchPtr patch = BuildOverlayPatch(source_name,
                                                  kNameFontHeight,
                                                  kNameFallbackScale,
                                                  kNameFallbackThickness);
        state.name_patches[source_name] = patch;
        return patch;
    }

    OverlayPatchPtr GetInfoPatch(const std::string &window_name,
                                 double fps,
                                 double infer_ms)
    {
        if (window_name.empty())
        {
            return OverlayPatchPtr();
        }

        char info[128];
        snprintf(info, sizeof(info), "FPS: %.1f | Infer: %.1f ms", fps, infer_ms);
        const std::string text(info);
        const auto now = std::chrono::steady_clock::now();

        OverlayCacheState &state = PatchState();
        std::lock_guard<std::mutex> lk(state.mutex);
        DynamicOverlayEntry &entry = state.info_patches[window_name];
        const bool needs_refresh =
            !entry.patch ||
            entry.last_refresh == std::chrono::steady_clock::time_point::min() ||
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - entry.last_refresh)
                    .count() >= kInfoRefreshIntervalMs;
        if (needs_refresh)
        {
            if (!entry.patch || entry.text != text)
            {
                entry.patch = BuildOverlayPatch(text,
                                                kInfoFontHeight,
                                                kInfoFallbackScale,
                                                kInfoFallbackThickness);
                entry.text = text;
            }
            entry.last_refresh = now;
        }
        return entry.patch;
    }

    void ClearOverlayStateForWindow(const std::string &window_name)
    {
        if (window_name.empty())
        {
            return;
        }
        OverlayCacheState &state = PatchState();
        std::lock_guard<std::mutex> lk(state.mutex);
        state.info_patches.erase(window_name);
    }

    struct GtkWallState
    {
        std::mutex mutex;
        bool started = false;
        bool display_seen_running = false;
        int window_x = 48;
        int window_y = 48;
        int wall_width = DISPLAY_WALL_WIDTH;
        int wall_height = DISPLAY_WALL_HEIGHT;
        bool fullscreen = false;
        int next_channel_id = 0;
        std::unordered_map<std::string, int> channel_map;
        std::thread thread;
        Display_t desc = {"dock_blindspot", 0, 0, DISPLAY_WALL_WIDTH, DISPLAY_WALL_HEIGHT, false};
        char **disp_map = nullptr;
    };

    struct CvWindowState
    {
        std::mutex mutex;
        std::unordered_map<std::string, bool> created_windows;
    };

    GtkWallState &WallState()
    {
        static GtkWallState state;
        return state;
    }

    CvWindowState &CvState()
    {
        static CvWindowState state;
        return state;
    }

    std::string &WindowTitleStore()
    {
        static std::string title = "dock_blindspot";
        return title;
    }

    int ResolveChannelIdLocked(GtkWallState *state, const std::string &window_name)
    {
        auto it = state->channel_map.find(window_name);
        if (it != state->channel_map.end())
        {
            return it->second;
        }
        const int channel_id = state->next_channel_id++;
        state->channel_map[window_name] = channel_id;
        grid_compositor_set_channel_count(std::max(1, state->next_channel_id));
        return channel_id;
    }

    bool EnsureDisplayStartedLocked(GtkWallState *state)
    {
        if (state->started)
        {
            return true;
        }

        const std::string &title = WindowTitleStore();
        state->desc.winTitle = title.c_str();
        state->desc.x = state->window_x;
        state->desc.y = state->window_y;
        state->desc.width = state->wall_width;
        state->desc.height = state->wall_height;
        state->desc.fullscreen = state->fullscreen;
        grid_compositor_set_display_size(state->desc.width, state->desc.height);

        state->disp_map = dispBufferMap(&state->desc);
        if (!state->disp_map || !(*state->disp_map))
        {
            return false;
        }

        if (0 != grid_compositor_init(state->disp_map, 1))
        {
            return false;
        }

        state->thread = std::thread([state]()
                                    { display(&state->desc); });
        state->thread.detach();
        state->started = true;
        return true;
    }

} // namespace

namespace modules
{
    namespace display
    {
        void DisplayNode::ConfigureGtkWindow(const GtkWindowOptions &options)
        {
            GtkWallState &state = WallState();
            std::lock_guard<std::mutex> lk(state.mutex);
            if (state.started)
            {
                LOGW("display already started, ignore gtk window reconfigure");
                return;
            }

            state.wall_width = options.width > 0 ? options.width : DISPLAY_WALL_WIDTH;
            state.wall_height = options.height > 0 ? options.height : DISPLAY_WALL_HEIGHT;
            state.window_x = std::max(0, options.x);
            state.window_y = std::max(0, options.y);
            state.fullscreen = options.fullscreen;

            std::string &title = WindowTitleStore();
            title = NormalizeWindowName(options.title);
        }

        void DisplayNode::InitWindow(const std::string &window_name) const
        {
            if (rga_debug_display_disabled())
            {
                LOGW("DISABLE_DISPLAY_RGA enabled");
                const std::string window = NormalizeWindowName(window_name);
                CvWindowState &cv_state = CvState();
                std::lock_guard<std::mutex> lk(cv_state.mutex);
                if (cv_state.created_windows.find(window) == cv_state.created_windows.end())
                {
                    cv::namedWindow(window, cv::WINDOW_NORMAL);
                    cv_state.created_windows[window] = true;
                }
                return;
            }

            const std::string window = NormalizeWindowName(window_name);
            GtkWallState &state = WallState();
            std::lock_guard<std::mutex> lk(state.mutex);
            if (!EnsureDisplayStartedLocked(&state))
            {
                return;
            }
            ResolveChannelIdLocked(&state, window);
        }

        bool DisplayNode::ShowFrame(const std::string &window_name,
                                    cv::Mat *frame,
                                    double fps,
                                    double infer_ms,
                                    const std::string &source_name) const
        {
            if (!frame || frame->empty())
                return false;
            const std::string window = NormalizeWindowName(window_name);
            const OverlayPatchPtr name_patch = GetOrCreateNamePatch(source_name);
            const bool show_info = !source_name.empty() || fps > 0.0 || infer_ms > 0.0;
            const OverlayPatchPtr info_patch = show_info ? GetInfoPatch(window, fps, infer_ms)
                                                         : OverlayPatchPtr();

            BlendOverlayPatch(frame, name_patch, cv::Point(kOverlayMarginPx, kOverlayMarginPx));
            int info_y = kOverlayMarginPx;
            if (name_patch && name_patch->IsValid())
            {
                info_y += name_patch->bgr.rows + kOverlayGapPx;
            }
            BlendOverlayPatch(frame, info_patch, cv::Point(kOverlayMarginPx, info_y));

            if (rga_debug_display_disabled())
            {
                CvWindowState &cv_state = CvState();
                std::lock_guard<std::mutex> lk(cv_state.mutex);
                if (cv_state.created_windows.find(window) == cv_state.created_windows.end())
                {
                    cv::namedWindow(window, cv::WINDOW_NORMAL);
                    cv_state.created_windows[window] = true;
                }
                cv::imshow(window, *frame);
                const int key = cv::waitKey(kCvWaitKeyDelayMs);
                if (key == 27 || key == 'q' || key == 'Q')
                {
                    return true;
                }
                return false;
            }

            int channel_id = 0;
            {
                GtkWallState &state = WallState();
                std::lock_guard<std::mutex> lk(state.mutex);
                if (!EnsureDisplayStartedLocked(&state))
                {
                    return false;
                }
                channel_id = ResolveChannelIdLocked(&state, window);
            }

            cv::Mat src;
            if (frame->isContinuous())
            {
                src = *frame;
            }
            else
            {
                src = frame->clone();
            }

            GridCompositorImgDesc_t img_desc = {};
            img_desc.chnId = channel_id;
            img_desc.width = src.cols;
            img_desc.height = src.rows;
            img_desc.horStride = static_cast<int>(src.step / src.elemSize());
            img_desc.verStride = src.rows;
            img_desc.dataSize = static_cast<int>(src.total() * src.elemSize());
            strncpy(img_desc.fmt, "BGR", sizeof(img_desc.fmt) - 1);
            grid_compositor_submit_frame(reinterpret_cast<char *>(src.data), img_desc);

            {
                GtkWallState &state = WallState();
                std::lock_guard<std::mutex> lk(state.mutex);
                if (displayIsRunning())
                {
                    state.display_seen_running = true;
                }
                if (state.display_seen_running && !displayIsRunning())
                {
                    return true;
                }
            }

            return false;
        }

        bool DisplayNode::ShowError(const std::string &window_name,
                                    const std::string &message) const
        {
            const std::string window = NormalizeWindowName(window_name);
            std::vector<std::string> lines = SplitLines(message);
            if (lines.empty())
            {
                lines.push_back("Unknown error");
            }

            const int width = 640;
            const int height = 360;
            cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(canvas, "ERROR",
                        cv::Point(20, 40),
                        cv::FONT_HERSHEY_SIMPLEX,
                        1.0, cv::Scalar(0, 0, 255), 2);

            int y = 90;
            for (const auto &line : lines)
            {
                cv::putText(canvas, line,
                            cv::Point(20, y),
                            cv::FONT_HERSHEY_SIMPLEX,
                            0.6, cv::Scalar(0, 0, 255), 1);
                y += 30;
                if (y > height - 20)
                    break;
            }

            return ShowFrame(window, &canvas, 0.0, 0.0);
        }

        void DisplayNode::CloseWindow(const std::string &window_name) const
        {
            const std::string window = NormalizeWindowName(window_name);
            ClearOverlayStateForWindow(window);
            if (rga_debug_display_disabled())
            {
                CvWindowState &cv_state = CvState();
                std::lock_guard<std::mutex> lk(cv_state.mutex);
                auto it = cv_state.created_windows.find(window);
                if (it != cv_state.created_windows.end())
                {
                    cv::destroyWindow(window);
                    cv_state.created_windows.erase(it);
                }
                return;
            }
            (void)window_name;
        }

    } // namespace display
} // namespace modules
