#ifndef APP_WORKER_POOL_H_
#define APP_WORKER_POOL_H_

#include <memory>
#include <vector>

#include "app_types.h"

struct WorkerPool {
    ResultQueue results;
    std::vector<std::unique_ptr<Worker>> workers;
};

bool init_worker_pool(const char* model_path,
                      int thread_count,
                      float conf_threshold,
                      WorkerPool* pool);

void stop_worker_pool(WorkerPool* pool);

#endif  // APP_WORKER_POOL_H_
