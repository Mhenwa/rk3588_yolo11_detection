#ifndef MODULES_DISPLAY_DISPLAY_NODE_H_
#define MODULES_DISPLAY_DISPLAY_NODE_H_

#include <string>

#include <opencv2/opencv.hpp>

namespace modules
{
    namespace display
    {
        struct GtkWindowOptions
        {
            std::string title;
            int width = 0;
            int height = 0;
            bool fullscreen = false;
        };

        class DisplayNode
        {
        public:
            static void ConfigureGtkWindow(const GtkWindowOptions &options);

            void InitWindow(const std::string &window_name) const;

            bool ShowFrame(const std::string &window_name,
                           cv::Mat *frame,
                           double fps,
                           double infer_ms,
                           const std::string &source_name = "") const;

            bool ShowError(const std::string &window_name,
                           const std::string &message) const;

            void CloseWindow(const std::string &window_name) const;
        };

    } // namespace display
} // namespace modules

#endif // MODULES_DISPLAY_DISPLAY_NODE_H_
