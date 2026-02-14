import galay.kernel;

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::kernel;
using namespace std::chrono_literals;

namespace {
std::atomic<bool> g_done{false};
std::atomic<long long> g_elapsedMs{0};

Coroutine sleepTask() {
    const auto start = std::chrono::steady_clock::now();

    co_await sleep(120ms);
    co_await sleep(180ms);

    const auto end = std::chrono::steady_clock::now();
    g_elapsedMs.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(),
        std::memory_order_release);
    g_done.store(true, std::memory_order_release);
    co_return;
}
}  // namespace

int main() {
    Runtime runtime(1, 1);
    runtime.start();

    auto* io = runtime.getNextIOScheduler();
    io->spawn(sleepTask());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!g_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    runtime.stop();

    std::cout << "timer-sleep import example elapsed(ms)="
              << g_elapsedMs.load() << "\n";
    return g_done.load(std::memory_order_acquire) ? 0 : 1;
}
