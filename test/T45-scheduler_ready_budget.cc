#include "galay-kernel/kernel/Coroutine.h"
#include "test/SchedulerTestAccess.h"

#include <atomic>
#include <iostream>

using namespace galay::kernel;

namespace {

std::atomic<int> g_completed{0};

Coroutine countingTask() {
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

template <typename SchedulerT>
bool verifyLocalReadyBudget(const char* label) {
    constexpr int kTaskCount = 300;

    g_completed.store(0, std::memory_order_relaxed);
    SchedulerT scheduler;

    for (int i = 0; i < kTaskCount; ++i) {
        Coroutine co = countingTask();
        detail::CoroutineAccess::setScheduler(co, &scheduler);
        SchedulerTestAccess::worker(scheduler).scheduleLocal(
            detail::CoroutineAccess::detachTask(std::move(co)));
        if (!SchedulerTestAccess::worker(scheduler).hasLocalWork()) {
            std::cerr << "[T45] " << label << " failed to enqueue local task " << i << "\n";
            return false;
        }
    }

    SchedulerTestAccess::processPending(scheduler);

    const int completed_after_first_pass = g_completed.load(std::memory_order_relaxed);
    if (completed_after_first_pass >= kTaskCount) {
        std::cerr << "[T45] " << label
                  << " should leave work for a later pass, completed="
                  << completed_after_first_pass << "\n";
        return false;
    }

    if (completed_after_first_pass <= 0) {
        std::cerr << "[T45] " << label << " did not execute any ready task\n";
        return false;
    }

    SchedulerTestAccess::processPending(scheduler);

    if (g_completed.load(std::memory_order_relaxed) != kTaskCount) {
        std::cerr << "[T45] " << label << " did not finish remaining ready tasks\n";
        return false;
    }

    return true;
}

bool verifyReadyBudget() {
#if defined(USE_KQUEUE)
    return verifyLocalReadyBudget<KqueueScheduler>("kqueue");
#elif defined(USE_EPOLL)
    return verifyLocalReadyBudget<EpollScheduler>("epoll");
#elif defined(USE_IOURING)
    return verifyLocalReadyBudget<IOUringScheduler>("io_uring");
#else
    std::cout << "T45-SchedulerReadyBudget SKIP\n";
    return true;
#endif
}

}  // namespace

int main() {
    if (!verifyReadyBudget()) {
        return 1;
    }

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    std::cout << "T45-SchedulerReadyBudget PASS\n";
#endif
    return 0;
}
