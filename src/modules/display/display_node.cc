#include "modules/display/display_node.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
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

    void DrawOverlayText(cv::Mat *frame,
                         const std::string &text,
                         const cv::Point &org,
                         int font_height,
                         double fallback_scale,
                         int fallback_thickness)
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
                               cv::Scalar(0, 255, 0),
                               -1,
                               cv::LINE_AA,
                               false);
            return;
        }
#endif
        cv::putText(*frame, text,
                    org,
                    cv::FONT_HERSHEY_SIMPLEX,
                    fallback_scale,
                    cv::Scalar(0, 255, 0),
                    fallback_thickness);
    }

    struct GtkWallState
    {
        std::mutex mutex;
        bool started = false;
        bool display_seen_running = false;
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

            char info[128];
            snprintf(info, sizeof(info), "FPS: %.1f | Infer: %.1f ms", fps, infer_ms);
            DrawOverlayText(frame, source_name,
                            cv::Point(10, 30),
                            30,
                            0.8,
                            2);
            DrawOverlayText(frame, info,
                            cv::Point(10, 80),
                            28,
                            0.8,
                            2);

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
            if (rga_debug_display_disabled())
            {
                const std::string window = NormalizeWindowName(window_name);
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
