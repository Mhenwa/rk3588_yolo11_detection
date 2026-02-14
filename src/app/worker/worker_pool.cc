#include "worker_pool.h"

#include <atomic>
#include <cstring>

#include "app_log.h"
#include "app_worker.h"

namespace {
std::atomic<unsigned int> g_core_rr{0};
}  // namespace

void stop_worker_pool(WorkerPool* pool)
{
    if (!pool) return;
    for (auto& w : pool->workers) {
        {
            std::lock_guard<std::mutex> lk(w->mtx);
            w->stop = true;
        }
        w->cv.notify_one();
    }
    for (auto& w : pool->workers) {
        if (w->th.joinable()) w->th.join();
    }
    for (auto& w : pool->workers) {
        release_yolo11_model(&w->ctx);
    }
    pool->workers.clear();
    {
        std::lock_guard<std::mutex> lk(pool->results.mtx);
        pool->results.items.clear();
    }
}

bool init_worker_pool(const char* model_path,
                      int thread_count,
                      float conf_threshold,
                      WorkerPool* pool)
{
    if (!model_path || !pool || thread_count < 1) return false;
    pool->workers.clear();
    pool->workers.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        auto worker = std::make_unique<Worker>();
        memset(&worker->ctx, 0, sizeof(rknn_app_context_t));
        worker->stop = false;
        worker->conf_threshold = conf_threshold;
        int core_idx = static_cast<int>(g_core_rr.fetch_add(1) % 3);
        if (core_idx == 0) worker->core_mask = RKNN_NPU_CORE_0;
        else if (core_idx == 1) worker->core_mask = RKNN_NPU_CORE_1;
        else worker->core_mask = RKNN_NPU_CORE_2;
        LOGI("Worker %d bind NPU core %d\n", i, core_idx);

        if (init_yolo11_model(model_path, &worker->ctx) != 0) {
            stop_worker_pool(pool);
            return false;
        }
        pool->workers.push_back(std::move(worker));
        Worker* started = pool->workers.back().get();
        if (rknn_set_core_mask(started->ctx.rknn_ctx, started->core_mask) != RKNN_SUCC) {
            LOGE("rknn_set_core_mask failed\n");
            stop_worker_pool(pool);
            return false;
        }
        started->th = std::thread(worker_loop, started, &pool->results);
    }
    return true;
}
