#ifndef CORE_QUEUE_RESULT_QUEUE_H_
#define CORE_QUEUE_RESULT_QUEUE_H_

#include "core/types/runtime_types.h"

/*
    非阻塞弹出一个结果
*/
bool try_pop_result(ResultQueue &q, FrameResult &out);

/*
    阻塞等待直到至少有一个结果
*/
FrameResult pop_result_wait(ResultQueue &q);

#endif // CORE_QUEUE_RESULT_QUEUE_H_
