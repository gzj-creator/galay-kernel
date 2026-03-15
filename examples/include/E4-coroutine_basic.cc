/**
 * @file E4-CoroutineBasic.cc
 * @brief Runtime task API basic example
 * @details Demonstrates Task<T>, Runtime::blockOn, Runtime::spawn and RuntimeHandle
 */

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include "test/StdoutLog.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace galay::kernel;

namespace {

std::atomic<int> g_detachedFinished{0};

Task<int> sumTask(int id, int limit)
{
    LogInfo("Task {} started", id);

    int sum = 0;
    for (int i = 0; i < limit; ++i) {
        sum += i;
    }

    LogInfo("Task {} completed, sum = {}", id, sum);
    co_return sum;
}

Task<void> detachedTask(int id)
{
    LogInfo("Detached task {} started", id);
    co_yield true;
    g_detachedFinished.fetch_add(1, std::memory_order_acq_rel);
    LogInfo("Detached task {} completed", id);
    co_return;
}

Task<void> spawnFromCurrentRuntime()
{
    LogInfo("Spawning detached tasks through RuntimeHandle::current()");
    auto runtimeHandle = RuntimeHandle::current();
    runtimeHandle.spawn(detachedTask(1));
    runtimeHandle.spawn(detachedTask(2));
    co_return;
}

Task<void> waitForDetachedTasks()
{
    for (int i = 0; i < 1024 && g_detachedFinished.load(std::memory_order_acquire) < 2; ++i) {
        co_yield true;
    }

    LogInfo("Detached tasks finished: {}", g_detachedFinished.load(std::memory_order_acquire));
    co_return;
}

Task<void> spawnBlockingDemo()
{
    auto blocking = RuntimeHandle::current().spawnBlocking([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return 7;
    });
    blocking.wait();
    int value = blocking.join();

    LogInfo("spawnBlocking returned {}", value);
    co_return;
}

} // namespace

int main()
{
    LogInfo("=== Runtime Task API Basic Example ===");

    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    LogInfo("\n--- Example 1: blockOn(Task<int>) ---");
    int rootValue = runtime.blockOn(sumTask(1, 1000));
    LogInfo("blockOn returned {}", rootValue);

    LogInfo("\n--- Example 2: spawn(Task<int>) -> JoinHandle<int> ---");
    auto handle = runtime.spawn(sumTask(2, 2000));
    handle.wait();
    LogInfo("spawn().join() returned {}", handle.join());

    LogInfo("\n--- Example 3: RuntimeHandle::current().spawn(...) ---");
    runtime.blockOn(spawnFromCurrentRuntime());
    runtime.blockOn(waitForDetachedTasks());

    LogInfo("\n--- Example 4: RuntimeHandle::spawnBlocking(...) ---");
    runtime.blockOn(spawnBlockingDemo());

    LogInfo("\n=== Example Completed ===");
    return 0;
}
