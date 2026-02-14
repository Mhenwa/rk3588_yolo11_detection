#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include <opencv2/opencv.hpp>

#include "app_core_types.h"
#include "yolo11.h"

struct FrameTask {
    int seq;
    cv::Mat frame;
    bool do_infer;
    std::chrono::steady_clock::time_point start_tp;
};

struct FrameResult {
    int seq;
    cv::Mat frame;
    std::chrono::steady_clock::time_point start_tp;
    double infer_ms;
    int detections;
};

struct ResultQueue {
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<FrameResult> items;
};

struct Worker {
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<FrameTask> tasks;
    std::thread th;
    rknn_app_context_t ctx;
    rknn_core_mask core_mask;
    float conf_threshold;
    bool stop;
};

#endif  // APP_TYPES_H_
