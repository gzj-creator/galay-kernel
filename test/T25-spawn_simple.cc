/**
 * @file T25-spawn_simple.cc
 * @brief 用途：验证基础 `spawn` 提交后任务能够被调度并顺利执行完成。
 * 关键覆盖点：任务提交、调度器接管执行、简单完成通知与收尾流程。
 * 通过条件：被提交任务确实执行完成，测试返回 0。
 */

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace galay::kernel;

namespace {

std::atomic<int> g_finishedTasks{0};

Task<int> computeTask(int id, int yields)
{
    assert(RuntimeHandle::tryCurrent().has_value());

    for (int i = 0; i < yields; ++i) {
        co_yield true;
    }

    g_finishedTasks.fetch_add(1, std::memory_order_acq_rel);
    co_return id * 10;
}

Task<void> spawnDetachedTasks()
{
    auto handle = RuntimeHandle::current().spawn(computeTask(2, 2));
    RuntimeHandle::current().spawn(computeTask(3, 3));
    (void)handle;
    co_return;
}

Task<void> waitForFinishedTasks(int expected)
{
    for (int i = 0; i < 4096 && g_finishedTasks.load(std::memory_order_acquire) < expected; ++i) {
        co_yield true;
    }

    assert(g_finishedTasks.load(std::memory_order_acquire) == expected);
    co_return;
}

Task<int> rootValue()
{
    co_return 42;
}

Task<void> spawnBlockingFromHandle()
{
    int value = RuntimeHandle::current().spawnBlocking([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return 7;
    }).join();

    assert(value == 7);
    co_return;
}

} // namespace

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    const int value = runtime.blockOn(rootValue());
    assert(value == 42);

    auto joined = runtime.spawn(computeTask(1, 1));
    assert(joined.join() == 10);

    runtime.blockOn(spawnDetachedTasks());
    runtime.blockOn(waitForFinishedTasks(3));
    runtime.blockOn(spawnBlockingFromHandle());

    std::cout << "T25-RuntimeTaskApiDemo PASS\n";
    return 0;
}
