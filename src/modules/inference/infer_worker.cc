#include "modules/inference/infer_worker.h"

#include <utility>

#include "core/queue/result_queue.h"
#include "modules/inference/infer_node.h"
#include "modules/postprocess/postprocess_node.h"
#include "modules/preprocess/preprocess_node.h"

void infer_worker_loop(Worker *worker, ResultQueue *results)
{
    modules::preprocess::PreprocessNode preprocess_node;
    modules::inference::InferNode infer_node;
    modules::postprocess::PostprocessNode postprocess_node;

    while (true)
    {
        FrameTask task;
        {
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
            modules::preprocess::PreprocessOutput preprocess_out;
            if (preprocess_node.Run(res.frame, worker->ctx, &preprocess_out))
            {
                modules::inference::InferOutput infer_out;
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
            std::lock_guard<std::mutex> lk(results->mtx);
            results->items.push_back(std::move(res));
        }
        results->cv.notify_one();
    }
}
