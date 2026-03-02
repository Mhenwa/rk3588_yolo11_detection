#include "core/pool/infer_worker.h"

#include <utility>

#include "core/queue/result_queue.h"
#include "modules/inference/infer_node.h"
#include "modules/postprocess/postprocess_node.h"
#include "modules/preprocess/preprocess_node.h"

void infer_worker_loop(Worker* worker, ResultQueue* results)
{
    // 每个 worker 线程各自持有节点实例，避免跨线程共享可变状态。
    modules::preprocess::PreprocessNode preprocess_node;
    modules::inference::InferNode infer_node;
    modules::postprocess::PostprocessNode postprocess_node;

    while (true)
    {
        FrameTask task;
        {
            // worker 在此等待：要么收到停止信号，要么拿到待处理帧任务。
            std::unique_lock<std::mutex> lk(worker->mtx);
            worker->cv.wait(lk, [&]
                            { return worker->stop || !worker->tasks.empty(); });
            if (worker->stop && worker->tasks.empty())
            {
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

        if (task.do_infer)
        {
            // 标准流程：预处理 -> NPU 推理 -> 后处理（框解码与叠加绘制）。
            modules::preprocess::PreprocessOutput preprocess_out;
            if (preprocess_node.Run(res.frame, worker->ctx, &preprocess_out))
            {
                core::types::InferOutput infer_out;
                if (infer_node.Run(&worker->ctx, preprocess_out.input_image,
                                   &infer_out))
                {
                    modules::postprocess::PostprocessOutput post_out;
                    if (postprocess_node.Run(&worker->ctx, &infer_out,
                                             preprocess_out.letterbox,
                                             worker->conf_threshold,
                                             &res.frame, &post_out))
                    {
                        res.infer_ms = infer_out.infer_ms;
                        res.detections = post_out.detection_count;
                    }
                }
            }
        }

        {
            // 把处理完成的帧写回结果队列，后续由 ResultQueue 做有序合并。
            std::lock_guard<std::mutex> lk(results->mtx);
            results->items.push_back(std::move(res));
        }
        results->cv.notify_one();
    }
}
