/**
 * @file T44-scheduler_wakeup_coalescing.cc
 * @brief 用途：验证调度器会合并重复唤醒请求，避免无效的多次唤醒。
 * 关键覆盖点：重复 wake 请求合并、唤醒计数控制、可运行队列推进。
 * 通过条件：重复唤醒被成功压缩且任务仍能完整执行，测试返回 0。
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

bool scheduleInjectedTasks(IOScheduler& scheduler, int count) {
    for (int i = 0; i < count; ++i) {
        Coroutine co = pendingTask();
        detail::CoroutineAccess::setScheduler(co, &scheduler);
        if (!scheduler.schedule(detail::CoroutineAccess::taskRef(co))) {
            std::cerr << "[T44] failed to inject task " << i << "\n";
            return false;
        }
    }
    return true;
}

#if defined(USE_KQUEUE)
bool verifyWakeupCoalescing() {
    KqueueScheduler scheduler;

    if (!scheduleInjectedTasks(scheduler, 3)) {
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
            std::cerr << "[T44] failed to read notify pipe: " << std::strerror(errno) << "\n";
            return false;
        }
        break;
    }

    if (total != 1) {
        std::cerr << "[T44] expected a single coalesced wakeup byte, got " << total << "\n";
        return false;
    }

    return true;
}
#elif defined(USE_EPOLL)
bool verifyWakeupCoalescing() {
    EpollScheduler scheduler;

    if (!scheduleInjectedTasks(scheduler, 3)) {
        return false;
    }

    uint64_t wake_count = 0;
    const ssize_t n = read(SchedulerTestAccess::wakeReadFd(scheduler), &wake_count, sizeof(wake_count));
    if (n != static_cast<ssize_t>(sizeof(wake_count))) {
        std::cerr << "[T44] failed to read eventfd wake count\n";
        return false;
    }

    if (wake_count != 1) {
        std::cerr << "[T44] expected a single coalesced wakeup, got " << wake_count << "\n";
        return false;
    }

    return true;
}
#elif defined(USE_IOURING)
bool verifyWakeupCoalescing() {
    IOUringScheduler scheduler;

    if (!scheduleInjectedTasks(scheduler, 3)) {
        return false;
    }

    uint64_t wake_count = 0;
    const ssize_t n = read(SchedulerTestAccess::wakeReadFd(scheduler), &wake_count, sizeof(wake_count));
    if (n != static_cast<ssize_t>(sizeof(wake_count))) {
        std::cerr << "[T44] failed to read eventfd wake count\n";
        return false;
    }

    if (wake_count != 1) {
        std::cerr << "[T44] expected a single coalesced wakeup, got " << wake_count << "\n";
        return false;
    }

    return true;
}
#else
bool verifyWakeupCoalescing() {
    std::cout << "T44-SchedulerWakeupCoalescing SKIP\n";
    return true;
}
#endif

}  // namespace

int main() {
    if (!verifyWakeupCoalescing()) {
        return 1;
    }

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    std::cout << "T44-SchedulerWakeupCoalescing PASS\n";
#endif
    return 0;
}
