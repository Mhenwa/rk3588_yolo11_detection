#ifndef MODULES_DISPLAY_DISPLAY_NODE_H_
#define MODULES_DISPLAY_DISPLAY_NODE_H_

#include <mutex>
#include <string>

#include <opencv2/opencv.hpp>

namespace modules
{
    namespace display
    {

        class DisplayNode
        {
        public:
            void InitWindow(const std::string &window_name) const;

            bool ShowFrame(const std::string &window_name,
                           cv::Mat *frame,
                           double fps,
                           double infer_ms) const;

            bool ShowError(const std::string &window_name,
                           const std::string &message) const;

            void CloseWindow(const std::string &window_name) const;

        private:
            static std::mutex &UiMutex();
        };

    } // namespace display
} // namespace modules

#endif // MODULES_DISPLAY_DISPLAY_NODE_H_
