/**
 * @file T60-benchmark_completion_latch.cc
 * @brief 用途：验证 benchmark `CompletionLatch` 的计数与唤醒行为。
 * 关键覆盖点：倒计数归零、等待线程唤醒、重复 arrive 的边界表现。
 * 通过条件：`CompletionLatch` 行为符合预期，测试返回 0。
 */

#include "benchmark/BenchmarkSync.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    galay::benchmark::CompletionLatch wake_latch(1);
    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        woke.store(wake_latch.waitFor(200ms), std::memory_order_release);
    });
    std::this_thread::sleep_for(20ms);
    wake_latch.arrive();
    waiter.join();
    if (!woke.load(std::memory_order_acquire)) {
        std::cerr << "[T60] waitFor should unblock once target is reached\n";
        return 1;
    }

    galay::benchmark::CompletionLatch ready_latch(2);
    ready_latch.arrive();
    ready_latch.arrive();
    if (!ready_latch.waitFor(1ms)) {
        std::cerr << "[T60] waitFor should return immediately when already satisfied\n";
        return 1;
    }

    galay::benchmark::CompletionLatch timeout_latch(2);
    timeout_latch.arrive();
    if (timeout_latch.waitFor(20ms)) {
        std::cerr << "[T60] waitFor should time out when target is not reached\n";
        return 1;
    }

    std::cout << "T60-BenchmarkCompletionLatch PASS\n";
    return 0;
}
