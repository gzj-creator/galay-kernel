/**
 * @file T48-runtime_spawn_join_handle.cc
 * @brief 用途：验证 `Runtime::spawn` 返回的 `JoinHandle` 可等待任务完成并取回结果。
 * 关键覆盖点：spawn 提交、JoinHandle 等待、结果读取与完成语义。
 * 通过条件：`JoinHandle` 正确返回结果且测试返回 0。
 */

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

    std::cout << "T48-RuntimeSpawnJoinHandle PASS\n";
    return 0;
}
