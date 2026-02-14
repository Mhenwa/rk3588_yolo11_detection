#ifndef APP_WORKER_H_
#define APP_WORKER_H_

#include "app_types.h"

bool try_pop_result(ResultQueue& q, FrameResult& out);
FrameResult pop_result_wait(ResultQueue& q);
void worker_loop(Worker* worker, ResultQueue* results);

#endif  // APP_WORKER_H_
