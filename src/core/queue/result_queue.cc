#include "core/queue/result_queue.h"

#include <utility>

bool try_pop_result(ResultQueue& q, FrameResult& out)
{
    std::lock_guard<std::mutex> lk(q.mtx);
    if (q.items.empty()) return false;
    out = std::move(q.items.front());
    q.items.pop_front();
    return true;
}

FrameResult pop_result_wait(ResultQueue& q)
{
    std::unique_lock<std::mutex> lk(q.mtx);
    q.cv.wait(lk, [&]{ return !q.items.empty(); });
    FrameResult out = std::move(q.items.front());
    q.items.pop_front();
    return out;
}
