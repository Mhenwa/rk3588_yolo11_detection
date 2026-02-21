#ifndef CORE_PIPELINE_FRAME_PIPELINE_H_
#define CORE_PIPELINE_FRAME_PIPELINE_H_

#include <map>
#include <memory>
#include <vector>

#include "core/types/app_types.h"

class FramePipeline
{
public:
    // 流水线通过单调递增的序号，把异步推理线程的结果重新整理为有序输出。
    FramePipeline(std::vector<std::unique_ptr<Worker>> *workers,
                  ResultQueue *result_queue);

    void Reset();
    void WaitForCapacity();
    void EnqueueFrame(const cv::Mat &frame,
                      std::chrono::steady_clock::time_point start_tp);
    bool PopReady(FrameResult *ready);
    bool WaitNextReady(FrameResult *ready);
    int in_flight() const { return in_flight_; }

private:
    // 共享的 worker 池与结果队列，由 AppController/WorkerPool 统一管理。
    std::vector<std::unique_ptr<Worker>> *workers_;
    ResultQueue *result_queue_; // 乱序返回的结果先暂存到这里，

    std::map<int, FrameResult> pending_; // 有序，等 next_seq_ 对应结果到达再输出

    // worker 调度与在途任务（in-flight）状态。
    int worker_count_;
    int seq_;           // 帧进入流水线时分配的序号。
    int next_seq_;      // 输出侧当前期望的下一个序号。
    int in_flight_;     // 尚未从 result_queue 回收的任务数量，不包含已经在 pending_ 里但还没按序输出的结果。
    int max_in_flight_; // 背压上限，用于控制内存占用与端到端延迟。
    int infer_rr_;      // 轮询分发时目标 worker 的索引。
};

#endif // CORE_PIPELINE_FRAME_PIPELINE_H_
