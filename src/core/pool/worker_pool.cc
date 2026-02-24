#include "core/pool/worker_pool.h"

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log/app_log.h"
#include "core/pool/infer_worker.h"
#include "modules/inference/infer_context.h"

namespace
{
    std::atomic<unsigned int> g_core_rr{0};

    int core_mask_to_index(rknn_core_mask mask)
    {
        if (mask == RKNN_NPU_CORE_0)
            return 0;
        if (mask == RKNN_NPU_CORE_1)
            return 1;
        if (mask == RKNN_NPU_CORE_2)
            return 2;
        return -1;
    }

    std::vector<rknn_core_mask> detect_available_npu_cores(rknn_context ctx)
    {
        const rknn_core_mask candidates[] = {
            RKNN_NPU_CORE_0,
            RKNN_NPU_CORE_1,
            RKNN_NPU_CORE_2,
        };

        std::vector<rknn_core_mask> available;
        for (rknn_core_mask mask : candidates)
        {
            int ret = rknn_set_core_mask(ctx, mask);
            if (ret == RKNN_SUCC)
            {
                available.push_back(mask);
            }
            else
            {
                LOGW("NPU core %d unavailable (ret=%d)\n", core_mask_to_index(mask), ret);
            }
        }

        if (available.empty())
        {
            LOGW("No dedicated NPU core available, fallback to RKNN_NPU_CORE_AUTO\n");
            available.push_back(RKNN_NPU_CORE_AUTO);
        }

        int ret = rknn_set_core_mask(ctx, RKNN_NPU_CORE_AUTO);
        if (ret != RKNN_SUCC)
        {
            LOGW("reset to RKNN_NPU_CORE_AUTO failed (ret=%d)\n", ret);
        }

        LOGI("Detected %zu usable NPU core mask(s)\n", available.size());
        return available;
    }
} // namespace

void stop_worker_pool(WorkerPool *pool)
{
    if (!pool)
        return;
    for (auto &w : pool->workers)
    {
        {
            std::lock_guard<std::mutex> lk(w->mtx);
            w->stop = true;
        }
        w->cv.notify_one();
    }
    for (auto &w : pool->workers)
    {
        if (w->th.joinable())
            w->th.join();
    }
    for (auto &w : pool->workers)
    {
        release_infer_context(&w->ctx);
    }
    pool->workers.clear();
    {
        std::lock_guard<std::mutex> lk(pool->results.mtx);
        pool->results.items.clear();
    }
}

bool init_worker_pool(const char *model_path,
                      int thread_count,
                      float conf_threshold,
                      WorkerPool *pool)
{
    if (!model_path || !pool || thread_count < 1)
        return false;
    pool->workers.clear();
    pool->workers.reserve(thread_count);
    std::vector<rknn_core_mask> available_masks;
    for (int i = 0; i < thread_count; ++i)
    {
        auto worker = std::make_unique<Worker>();
        memset(&worker->ctx, 0, sizeof(rknn_app_context_t));
        worker->stop = false;
        worker->conf_threshold = conf_threshold;

        if (init_infer_context(model_path, &worker->ctx) != 0)
        {
            stop_worker_pool(pool);
            return false;
        }

        if (available_masks.empty())
        {
            available_masks = detect_available_npu_cores(worker->ctx.rknn_ctx);
        }

        // 优先轮询绑定核心
        const int core_slot = static_cast<int>(g_core_rr.fetch_add(1) % available_masks.size());
        worker->core_mask = available_masks[core_slot];

        int ret = rknn_set_core_mask(worker->ctx.rknn_ctx, worker->core_mask);
        if (ret != RKNN_SUCC)
        {
            LOGW("rknn_set_core_mask failed (ret=%d), fallback to RKNN_NPU_CORE_AUTO\n", ret);
            worker->core_mask = RKNN_NPU_CORE_AUTO;
            if (rknn_set_core_mask(worker->ctx.rknn_ctx, worker->core_mask) != RKNN_SUCC)
            {
                LOGE("rknn_set_core_mask RKNN_NPU_CORE_AUTO failed\n");
                stop_worker_pool(pool);
                return false;
            }
        }

        const int core_idx = core_mask_to_index(worker->core_mask);
        if (core_idx >= 0)
            LOGI("Worker %d bind NPU core %d\n", i, core_idx);
        else
            LOGI("Worker %d bind NPU core auto\n", i);

        pool->workers.push_back(std::move(worker));
        Worker *started = pool->workers.back().get();
        if (started == nullptr)
        {
            stop_worker_pool(pool);
            return false;
        }
        started->th = std::thread(infer_worker_loop, started, &pool->results);
    }
    return true;
}
