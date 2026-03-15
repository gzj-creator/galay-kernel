#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <atomic>
#include <cassert>
#include <iostream>

using namespace galay::kernel;

namespace {
std::atomic<bool> g_detached_done{false};
}

Task<int> sumTask()
{
    co_return 7;
}

Task<void> detachedTask()
{
    g_detached_done.store(true, std::memory_order_release);
    co_return;
}

Task<void> waitForDetached()
{
    for (int i = 0; i < 1024 && !g_detached_done.load(std::memory_order_acquire); ++i) {
        co_yield true;
    }
    assert(g_detached_done.load(std::memory_order_acquire));
    co_return;
}

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();
    runtime.start();

    {
        auto handle = runtime.spawn(sumTask());
        assert(handle.join() == 7);
    }

    {
        auto detached = runtime.spawn(detachedTask());
    }

    runtime.blockOn(waitForDetached());
    runtime.stop();

    std::cout << "T54-RuntimeSpawnJoinHandle PASS\n";
    return 0;
}
