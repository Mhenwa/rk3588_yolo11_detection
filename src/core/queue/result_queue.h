#ifndef CORE_QUEUE_RESULT_QUEUE_H_
#define CORE_QUEUE_RESULT_QUEUE_H_

#include "core/types/app_types.h"

bool try_pop_result(ResultQueue& q, FrameResult& out);
FrameResult pop_result_wait(ResultQueue& q);

#endif  // CORE_QUEUE_RESULT_QUEUE_H_
