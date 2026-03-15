#include "galay-kernel/kernel/Runtime.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::kernel;

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();
    runtime.start();

    auto start = std::chrono::steady_clock::now();

    auto first = runtime.spawnBlocking([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 11;
    });
    auto second = runtime.spawnBlocking([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 31;
    });

    const int first_result = first.join();
    const int second_result = second.join();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    assert(first_result == 11);
    assert(second_result == 31);
    assert(elapsed_ms < 190 && "spawnBlocking tasks should execute concurrently");

    runtime.stop();

    std::cout << "T56-RuntimeSpawnBlocking PASS\n";
    return 0;
}
