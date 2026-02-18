#ifndef CORE_PIPELINE_FRAME_PIPELINE_H_
#define CORE_PIPELINE_FRAME_PIPELINE_H_

#include <map>
#include <memory>
#include <vector>

#include "core/types/app_types.h"

class FramePipeline {
public:
    FramePipeline(std::vector<std::unique_ptr<Worker>>* workers,
                  ResultQueue* result_queue);

    void Reset();
    void WaitForCapacity();
    void EnqueueFrame(const cv::Mat& frame,
                      std::chrono::steady_clock::time_point start_tp);
    bool PopReady(FrameResult* ready);
    bool WaitNextReady(FrameResult* ready);
    int in_flight() const { return in_flight_; }

private:
    std::vector<std::unique_ptr<Worker>>* workers_;
    ResultQueue* result_queue_;
    std::map<int, FrameResult> pending_;
    int worker_count_;
    int seq_;
    int next_seq_;
    int in_flight_;
    int max_in_flight_;
    int infer_rr_;
};

#endif  // CORE_PIPELINE_FRAME_PIPELINE_H_
