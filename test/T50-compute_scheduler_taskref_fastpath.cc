#include "galay-kernel/kernel/ComputeScheduler.h"

#include <atomic>
#include <concepts>
#include <iostream>
#include <type_traits>

using namespace galay::kernel;

namespace {

static_assert(std::same_as<decltype(ComputeTask{}.task), TaskRef>,
              "ComputeTask should carry TaskRef for fast-path scheduling");

std::atomic<int> g_completed{0};

Coroutine countingTask() {
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

bool verifyTaskRefFastPath() {
    g_completed.store(0, std::memory_order_relaxed);

    ComputeScheduler scheduler;
    scheduler.start();

    Coroutine co = countingTask();
    detail::CoroutineAccess::setScheduler(co, &scheduler);
    if (!scheduler.schedule(detail::CoroutineAccess::taskRef(co))) {
        std::cerr << "[T50] schedule(TaskRef) rejected valid compute task\n";
        scheduler.stop();
        return false;
    }

    scheduler.stop();

    if (g_completed.load(std::memory_order_relaxed) != 1) {
        std::cerr << "[T50] expected compute task to complete once\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!verifyTaskRefFastPath()) {
        return 1;
    }

    std::cout << "T50-ComputeSchedulerTaskRefFastPath PASS\n";
    return 0;
}
