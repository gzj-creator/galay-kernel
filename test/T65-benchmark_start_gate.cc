#include "benchmark/BenchmarkSync.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::benchmark;
using namespace std::chrono_literals;

int main() {
    StartGate gate;
    std::atomic<bool> released{false};

    std::thread waiter([&]() {
        gate.wait();
        released.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(20ms);
    if (released.load(std::memory_order_acquire)) {
        std::cerr << "[T65] wait() should block before gate opens\n";
        waiter.join();
        return 1;
    }

    if (gate.waitFor(10ms)) {
        std::cerr << "[T65] waitFor() should time out while gate is closed\n";
        waiter.join();
        return 1;
    }

    gate.open();

    if (!gate.waitFor(10ms)) {
        std::cerr << "[T65] waitFor() should succeed once gate is open\n";
        waiter.join();
        return 1;
    }

    waiter.join();

    if (!released.load(std::memory_order_acquire)) {
        std::cerr << "[T65] waiting thread should be released once gate opens\n";
        return 1;
    }

    std::cout << "T65-BenchmarkStartGate PASS\n";
    return 0;
}
