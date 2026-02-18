#ifndef MODULES_INFERENCE_INFER_WORKER_H_
#define MODULES_INFERENCE_INFER_WORKER_H_

#include "core/types/app_types.h"

void infer_worker_loop(Worker* worker, ResultQueue* results);

#endif  // MODULES_INFERENCE_INFER_WORKER_H_
