#ifndef MODES_FPS_TRACKER_H_
#define MODES_FPS_TRACKER_H_

#include <chrono>

struct FpsTracker {
    int counter = 0;
    double fps_calc = 0.0;
    double display_fps = 0.0;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    double on_frame(bool is_infer_frame)
    {
        counter++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed >= 1.0) {
            fps_calc = counter / elapsed;
            counter = 0;
            t0 = now;
        }
        if (is_infer_frame) {
            display_fps = fps_calc;
        }
        return display_fps;
    }
};

#endif  // MODES_FPS_TRACKER_H_
