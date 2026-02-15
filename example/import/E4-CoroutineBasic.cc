import galay.kernel;

#include <coroutine>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::kernel;

namespace {
std::atomic<int> g_tasks_finished{0};
std::atomic<bool> g_wait_done{false};

Coroutine simpleTask(int id, int limit) {
    long long sum = 0;
    for (int i = 0; i < limit; ++i) {
        sum += i;
    }

    std::cout << "task " << id << " finished, sum=" << sum << "\n";
    g_tasks_finished.fetch_add(1, std::memory_order_release);
    co_return;
}

Coroutine parentTask() {
    co_await spawn(simpleTask(2, 1000));
    co_await spawn(simpleTask(3, 2000));
    co_return;
}

Coroutine waitExample() {
    Coroutine task = simpleTask(100, 3000);
    co_await task.wait();
    g_wait_done.store(true, std::memory_order_release);
    co_return;
}
}  // namespace

int main() {
    ComputeScheduler scheduler;
    scheduler.start();

    scheduler.spawn(simpleTask(1, 500));
    scheduler.spawn(parentTask());
    scheduler.spawn(waitExample());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!g_wait_done.load(std::memory_order_acquire) ||
            g_tasks_finished.load(std::memory_order_acquire) < 4) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();
    std::cout << "finished task count: " << g_tasks_finished.load() << "\n";
    return 0;
}
