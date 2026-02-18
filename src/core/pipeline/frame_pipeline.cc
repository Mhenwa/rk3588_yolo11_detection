#include "core/pipeline/frame_pipeline.h"

#include <utility>

#include "core/queue/result_queue.h"

FramePipeline::FramePipeline(std::vector<std::unique_ptr<Worker>>* workers,
                             ResultQueue* result_queue)
    : workers_(workers),
      result_queue_(result_queue),
      worker_count_(0),
      seq_(0),
      next_seq_(0),
      in_flight_(0),
      max_in_flight_(0),
      infer_rr_(0)
{
    Reset();
}

void FramePipeline::Reset()
{
    pending_.clear();
    worker_count_ = workers_ ? static_cast<int>(workers_->size()) : 0;
    seq_ = 0;
    next_seq_ = 0;
    in_flight_ = 0;
    max_in_flight_ = worker_count_ * 2;
    infer_rr_ = 0;
}

void FramePipeline::WaitForCapacity()
{
    if (max_in_flight_ <= 0 || in_flight_ < max_in_flight_) return;
    FrameResult res = pop_result_wait(*result_queue_);
    pending_.emplace(res.seq, std::move(res));
    in_flight_--;
}

void FramePipeline::EnqueueFrame(const cv::Mat& frame,
                                 std::chrono::steady_clock::time_point start_tp)
{
    if (!workers_ || workers_->empty()) return;
    FrameTask task;
    task.seq = seq_++;
    task.do_infer = true;
    task.frame = frame.clone();
    task.start_tp = start_tp;

    int target_idx = infer_rr_;
    infer_rr_ = (infer_rr_ + 1) % worker_count_;
    Worker* target = (*workers_)[target_idx].get();
    {
        std::lock_guard<std::mutex> lk(target->mtx);
        target->tasks.push_back(std::move(task));
    }
    target->cv.notify_one();
    in_flight_++;
}

bool FramePipeline::PopReady(FrameResult* ready)
{
    if (!ready) return false;
    FrameResult res;
    while (try_pop_result(*result_queue_, res)) {
        pending_.emplace(res.seq, std::move(res));
        in_flight_--;
    }
    auto it = pending_.find(next_seq_);
    if (it == pending_.end()) return false;
    *ready = std::move(it->second);
    pending_.erase(it);
    next_seq_++;
    return true;
}

bool FramePipeline::WaitNextReady(FrameResult* ready)
{
    if (!ready) return false;
    while (true) {
        if (PopReady(ready)) return true;
        FrameResult res = pop_result_wait(*result_queue_);
        pending_.emplace(res.seq, std::move(res));
        in_flight_--;
    }
}
