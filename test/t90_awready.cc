/**
 * @file t90_awready.cc
 * @brief 用途：验证 `AwaitableBuilder::ready(...)` 在无 IO scheduler 场景下可立即返回。
 * 关键覆盖点：ready awaitable 的直接返回与 `.timeout(...)` 包装行为。
 * 通过条件：`Runtime::blockOn(...)` 返回预期值且测试返回 0。
 */

#include "galay-kernel/kernel/awaitable.h"
#include "galay-kernel/kernel/runtime.h"
#include "galay-kernel/kernel/task.h"

#include <chrono>
#include <expected>
#include <iostream>

using namespace galay::kernel;
using namespace std::chrono_literals;

using ReadyResult = std::expected<int, IOError>;

Task<ReadyResult> readyTask()
{
    auto result = co_await AwaitableBuilder<ReadyResult>::ready(ReadyResult{42});
    co_return result;
}

Task<ReadyResult> readyTimeoutTask()
{
    auto result = co_await AwaitableBuilder<ReadyResult>::ready(ReadyResult{77}).timeout(5ms);
    co_return result;
}

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    const auto direct = runtime.blockOn(readyTask());
    if (!direct || direct.value() != 42) {
        std::cerr << "direct ready awaitable returned unexpected result\n";
        return 1;
    }

    const auto timed = runtime.blockOn(readyTimeoutTask());
    if (!timed || timed.value() != 77) {
        std::cerr << "timed ready awaitable returned unexpected result\n";
        return 1;
    }

    std::cout << "T90-AwaitableBuilderReadyRuntime PASS\n";
    return 0;
}
