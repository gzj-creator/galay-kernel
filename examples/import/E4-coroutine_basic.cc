import galay.kernel;

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::kernel;

namespace {

std::atomic<int> g_detachedFinished{0};

Task<int> sumTask(int id, int limit)
{
    std::cout << "task " << id << " started\n";

    int sum = 0;
    for (int i = 0; i < limit; ++i) {
        sum += i;
    }

    std::cout << "task " << id << " completed, sum=" << sum << "\n";
    co_return sum;
}

Task<void> detachedTask(int id)
{
    std::cout << "detached task " << id << " started\n";
    co_yield true;
    g_detachedFinished.fetch_add(1, std::memory_order_acq_rel);
    std::cout << "detached task " << id << " completed\n";
    co_return;
}

Task<void> spawnFromCurrentRuntime()
{
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

    std::cout << "detached tasks finished: " << g_detachedFinished.load(std::memory_order_acquire) << "\n";
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

    std::cout << "spawnBlocking returned " << value << "\n";
    co_return;
}

} // namespace

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    int rootValue = runtime.blockOn(sumTask(1, 1000));
    std::cout << "blockOn returned " << rootValue << "\n";

    auto handle = runtime.spawn(sumTask(2, 2000));
    handle.wait();
    std::cout << "spawn().join() returned " << handle.join() << "\n";

    runtime.blockOn(spawnFromCurrentRuntime());
    runtime.blockOn(waitForDetachedTasks());
    runtime.blockOn(spawnBlockingDemo());

    return 0;
}
