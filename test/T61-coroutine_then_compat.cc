/**
 * @file T61-coroutine_then_compat.cc
 * @brief 用途：验证旧式 coroutine `then` 链式调用在当前实现下仍能正确工作。
 * 关键覆盖点：`then` 兼容层、链式 continuation、结果顺序与完成通知。
 * 通过条件：兼容调用链成功执行，测试返回 0。
 */

#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/kernel/Coroutine.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace galay::kernel;

namespace {

std::mutex g_sequence_mutex;
std::vector<int> g_sequence;
std::atomic<int> g_completed{0};

Coroutine pushStep(int step) {
    {
        std::lock_guard<std::mutex> lock(g_sequence_mutex);
        g_sequence.push_back(step);
    }
    g_completed.fetch_add(1, std::memory_order_release);
    co_return;
}

}  // namespace

static_assert(std::is_same_v<decltype(std::declval<Coroutine&>().then(pushStep(2))), Coroutine&>);
static_assert(std::is_same_v<decltype(std::declval<Coroutine>().then(pushStep(2))), Coroutine&&>);

int main() {
    ComputeScheduler scheduler;
    scheduler.start();

    scheduler.spawn(pushStep(1).then(pushStep(2)));

    for (int i = 0; i < 100 && g_completed.load(std::memory_order_acquire) < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    assert(g_completed.load(std::memory_order_acquire) == 2);
    assert((g_sequence == std::vector<int>{1, 2}));

    std::cout << "T61-CoroutineThenCompat PASS\n";
    return 0;
}
