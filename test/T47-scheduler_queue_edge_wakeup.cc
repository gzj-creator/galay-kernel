/**
 * @file T47-scheduler_queue_edge_wakeup.cc
 * @brief 用途：验证队列从空到非空的边沿变化会触发正确的唤醒语义。
 * 关键覆盖点：空队列边沿检测、首次入队唤醒、重复入队避免冗余 wake。
 * 通过条件：边沿唤醒语义成立且测试返回 0。
 */

#include "galay-kernel/kernel/Coroutine.h"
#include "test/SchedulerTestAccess.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unistd.h>

using namespace galay::kernel;

namespace {

Coroutine pendingTask() {
    co_return;
}

template <typename SchedulerT>
bool injectBurstFromEmptyQueue(SchedulerT& scheduler, int count) {
    SchedulerTestAccess::sleeping(scheduler).store(false, std::memory_order_release);
    SchedulerTestAccess::wakeupPending(scheduler).store(false, std::memory_order_release);

    for (int i = 0; i < count; ++i) {
        Coroutine co = pendingTask();
        detail::CoroutineAccess::setScheduler(co, &scheduler);
        if (!scheduler.schedule(detail::CoroutineAccess::taskRef(co))) {
            std::cerr << "[T47] failed to inject task " << i << "\n";
            return false;
        }
    }
    return true;
}

#if defined(USE_KQUEUE)
bool verifyQueueEdgeWakeup() {
    KqueueScheduler scheduler;

    if (!injectBurstFromEmptyQueue(scheduler, 3)) {
        return false;
    }

    char buffer[32];
    ssize_t total = 0;
    while (true) {
        const ssize_t n = read(SchedulerTestAccess::wakeReadFd(scheduler), buffer, sizeof(buffer));
        if (n > 0) {
            total += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0) {
            std::cerr << "[T47] failed to read notify pipe: " << std::strerror(errno) << "\n";
            return false;
        }
        break;
    }

    if (total != 1) {
        std::cerr << "[T47] expected a single edge-triggered wakeup byte, got " << total << "\n";
        return false;
    }

    return true;
}
#elif defined(USE_EPOLL)
bool verifyQueueEdgeWakeup() {
    EpollScheduler scheduler;

    if (!injectBurstFromEmptyQueue(scheduler, 3)) {
        return false;
    }

    uint64_t wake_count = 0;
    const ssize_t n = read(SchedulerTestAccess::wakeReadFd(scheduler), &wake_count, sizeof(wake_count));
    if (n != static_cast<ssize_t>(sizeof(wake_count))) {
        std::cerr << "[T47] failed to read eventfd wake count\n";
        return false;
    }

    if (wake_count != 1) {
        std::cerr << "[T47] expected a single edge-triggered wakeup, got " << wake_count << "\n";
        return false;
    }

    return true;
}
#elif defined(USE_IOURING)
bool verifyQueueEdgeWakeup() {
    IOUringScheduler scheduler;

    if (!injectBurstFromEmptyQueue(scheduler, 3)) {
        return false;
    }

    uint64_t wake_count = 0;
    const ssize_t n = read(SchedulerTestAccess::wakeReadFd(scheduler), &wake_count, sizeof(wake_count));
    if (n != static_cast<ssize_t>(sizeof(wake_count))) {
        std::cerr << "[T47] failed to read eventfd wake count\n";
        return false;
    }

    if (wake_count != 1) {
        std::cerr << "[T47] expected a single edge-triggered wakeup, got " << wake_count << "\n";
        return false;
    }

    return true;
}
#else
bool verifyQueueEdgeWakeup() {
    std::cout << "T47-SchedulerQueueEdgeWakeup SKIP\n";
    return true;
}
#endif

}  // namespace

int main() {
    if (!verifyQueueEdgeWakeup()) {
        return 1;
    }

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    std::cout << "T47-SchedulerQueueEdgeWakeup PASS\n";
#endif
    return 0;
}
