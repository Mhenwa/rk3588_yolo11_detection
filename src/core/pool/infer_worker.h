#ifndef CORE_POOL_INFER_WORKER_H_
#define CORE_POOL_INFER_WORKER_H_

#include "core/types/app_types.h"

void infer_worker_loop(Worker* worker, ResultQueue* results);

#endif  // CORE_POOL_INFER_WORKER_H_
