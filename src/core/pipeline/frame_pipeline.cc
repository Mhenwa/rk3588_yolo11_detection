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
    // 每次新流启动或复用流水线实例时，重置有序输出与背压相关状态。
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
    // 简单背压：当在途帧过多时阻塞等待，直到至少回收一个 worker 结果。
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
    // 通过轮询把任务分发到各 worker，尽量保持负载均衡。
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
    // 先尽可能取回当前可用结果，再只输出 next_seq_ 对应帧，保证显示顺序稳定。
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
    // 用于收尾阶段的阻塞接口：不管 worker 完成先后，始终等待下一个有序结果。
    if (!ready) return false;
    while (true) {
        if (PopReady(ready)) return true;
        FrameResult res = pop_result_wait(*result_queue_);
        pending_.emplace(res.seq, std::move(res));
        in_flight_--;
    }
}
