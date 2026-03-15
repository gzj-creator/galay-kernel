/**
 * @file T59-benchmark_sync_wait_ready.cc
 * @brief 用途：验证 benchmark 同步辅助中的 `waitReady` 协调逻辑。
 * 关键覆盖点：启动就绪等待、超时控制、并发参与者对齐。
 * 通过条件：`waitReady` 按预期同步参与方，测试返回 0。
 */

#include "benchmark/BenchmarkSync.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> already_ready{true};
    if (!galay::benchmark::waitForFlag(already_ready, 5ms)) {
        std::cerr << "[T59] already-ready flag should succeed immediately\n";
        return 1;
    }

    std::atomic<bool> delayed_ready{false};
    std::thread delayed_setter([&]() {
        std::this_thread::sleep_for(20ms);
        delayed_ready.store(true, std::memory_order_release);
    });

    const bool delayed_result = galay::benchmark::waitForFlag(delayed_ready, 200ms, 1ms);
    delayed_setter.join();
    if (!delayed_result) {
        std::cerr << "[T59] delayed-ready flag should succeed before timeout\n";
        return 1;
    }

    std::atomic<bool> never_ready{false};
    if (galay::benchmark::waitForFlag(never_ready, 30ms, 5ms)) {
        std::cerr << "[T59] never-ready flag should time out\n";
        return 1;
    }

    std::cout << "T59-BenchmarkSyncWaitReady PASS\n";
    return 0;
}
