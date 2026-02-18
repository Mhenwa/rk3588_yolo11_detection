/*
    cv::imshow
    Todo qt5
*/

#include "modules/display/display_node.h"

#include <cstdio>
#include <sstream>
#include <vector>

namespace
{

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

} // namespace

namespace modules
{
    namespace display
    {

        std::mutex &DisplayNode::UiMutex()
        {
            static std::mutex ui_mutex;
            return ui_mutex;
        }

        void DisplayNode::InitWindow(const std::string &window_name) const
        {
            const std::string window = NormalizeWindowName(window_name);
            std::lock_guard<std::mutex> lk(UiMutex());
            cv::namedWindow(window.c_str(), cv::WINDOW_NORMAL | cv::WINDOW_FREERATIO);
            cv::setWindowProperty(window.c_str(), cv::WND_PROP_AUTOSIZE, 0);
        }

        bool DisplayNode::ShowFrame(const std::string &window_name,
                                    cv::Mat *frame,
                                    double fps,
                                    double infer_ms) const
        {
            if (!frame || frame->empty())
                return false;
            const std::string window = NormalizeWindowName(window_name);

            char info[128];
            snprintf(info, sizeof(info), "FPS: %.1f | Infer: %.1f ms", fps, infer_ms);
            cv::putText(*frame, info,
                        cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.8, cv::Scalar(0, 255, 0), 2);

            std::lock_guard<std::mutex> lk(UiMutex());
            cv::imshow(window.c_str(), *frame);
            return cv::waitKey(1) == 27;
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

            std::lock_guard<std::mutex> lk(UiMutex());
            cv::namedWindow(window.c_str(), cv::WINDOW_NORMAL | cv::WINDOW_FREERATIO);
            cv::setWindowProperty(window.c_str(), cv::WND_PROP_AUTOSIZE, 0);
            cv::imshow(window.c_str(), canvas);
            return cv::waitKey(1) == 27;
        }

        void DisplayNode::CloseWindow(const std::string &window_name) const
        {
            const std::string window = NormalizeWindowName(window_name);
            std::lock_guard<std::mutex> lk(UiMutex());
            cv::destroyWindow(window.c_str());
        }

    } // namespace display
} // namespace modules
