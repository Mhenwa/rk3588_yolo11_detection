#include "app_worker.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

#include "image_utils.h"
#include "postprocess.h"

static void draw_results(cv::Mat& frame, const object_detect_result_list& results)
{
    for (int i = 0; i < results.count; i++) {
        auto* det = &results.results[i];
        cv::rectangle(frame,
            cv::Point(det->box.left, det->box.top),
            cv::Point(det->box.right, det->box.bottom),
            cv::Scalar(255, 0, 0), 2);

        char label[128];
        snprintf(label, sizeof(label), "%s %.1f%%",
            coco_cls_to_name(det->cls_id),
            det->prop * 100);

        cv::putText(frame, label,
            cv::Point(det->box.left, det->box.top - 5),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5, cv::Scalar(0, 0, 255), 1);
    }
}

bool try_pop_result(ResultQueue& q, FrameResult& out)
{
    std::lock_guard<std::mutex> lk(q.mtx);
    if (q.items.empty()) return false;
    out = std::move(q.items.front());
    q.items.pop_front();
    return true;
}

FrameResult pop_result_wait(ResultQueue& q)
{
    std::unique_lock<std::mutex> lk(q.mtx);
    q.cv.wait(lk, [&]{ return !q.items.empty(); });
    FrameResult out = std::move(q.items.front());
    q.items.pop_front();
    return out;
}

void worker_loop(Worker* worker, ResultQueue* results)
{
    while (true) {
        FrameTask task;
        {
            std::unique_lock<std::mutex> lk(worker->mtx);
            worker->cv.wait(lk, [&]{ return worker->stop || !worker->tasks.empty(); });
            if (worker->stop && worker->tasks.empty()) {
                break;
            }
            task = std::move(worker->tasks.front());
            worker->tasks.pop_front();
        }

        FrameResult res;
        res.seq = task.seq;
        res.frame = std::move(task.frame);
        res.start_tp = task.start_tp;
        res.infer_ms = 0.0;
        res.detections = 0;

        if (task.do_infer) {
            image_buffer_t img;
            memset(&img, 0, sizeof(img));
            img.width = res.frame.cols;
            img.height = res.frame.rows;
#if defined(DISABLE_RGA)
            img.format = IMAGE_FORMAT_RGB888;
            cv::Mat rgb;
            cv::cvtColor(res.frame, rgb, cv::COLOR_BGR2RGB);
            img.virt_addr = rgb.data;
#else
            img.format = IMAGE_FORMAT_BGR888;
            img.virt_addr = res.frame.data;
#endif
            object_detect_result_list det_results;
            auto t0 = std::chrono::steady_clock::now();
            inference_yolo11_model(&worker->ctx, &img, &det_results,
                                   worker->conf_threshold);
            auto t1 = std::chrono::steady_clock::now();

            res.infer_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            res.detections = det_results.count;

            draw_results(res.frame, det_results);
        }

        {
            std::lock_guard<std::mutex> lk(results->mtx);
            results->items.push_back(std::move(res));
        }
        results->cv.notify_one();
    }
}
