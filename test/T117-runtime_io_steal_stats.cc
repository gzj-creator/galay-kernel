/**
 * @file T117-runtime_io_steal_stats.cc
 * @brief 用途：验证 Runtime 能暴露 IOScheduler stealing 统计。
 * 关键覆盖点：steal attempts/successes 快照、skewed load 下成功计数增长。
 * 通过条件：统计结构可读取，且至少一个 scheduler 记录到成功 stealing。
 */

#include "galay-kernel/common/TimerManager.hpp"
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/Task.h"

#if defined(USE_KQUEUE)
#include "galay-kernel/kernel/KqueueScheduler.h"
using IOSchedulerType = galay::kernel::KqueueScheduler;
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
using IOSchedulerType = galay::kernel::EpollScheduler;
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/IOUringScheduler.h"
using IOSchedulerType = galay::kernel::IOUringScheduler;
#else
#error "T117-RuntimeIOStealStats requires kqueue, epoll, or io_uring"
#endif

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace galay::kernel;
using namespace std::chrono_literals;

namespace {

bool waitUntil(auto&& predicate,
               std::chrono::milliseconds timeout = 2000ms,
               std::chrono::milliseconds step = 1ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(step);
    }
    return predicate();
}

void waitForFlag(const std::atomic<bool>& flag) {
    while (!flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

struct ScenarioState {
    std::atomic<bool> release{false};
    std::atomic<int> ran_on_sibling{0};
    std::atomic<int> completed{0};
    IOScheduler* source = nullptr;
    IOScheduler* sibling = nullptr;
};

Task<void> backlogTask(ScenarioState* state) {
    const auto tid = std::this_thread::get_id();
    if (state->sibling && tid == state->sibling->threadId()) {
        state->ran_on_sibling.fetch_add(1, std::memory_order_relaxed);
    }
    waitForFlag(state->release);
    state->completed.fetch_add(1, std::memory_order_release);
    co_return;
}

bool runStatsScenario() {
    constexpr int kTaskCount = 64;

    Runtime runtime;
    auto source = std::make_unique<IOSchedulerType>();
    auto sibling = std::make_unique<IOSchedulerType>();
    source->replaceTimerManager(TimingWheelTimerManager(1'000'000ULL));
    sibling->replaceTimerManager(TimingWheelTimerManager(1'000'000ULL));
    auto* source_ptr = source.get();
    auto* sibling_ptr = sibling.get();
    runtime.addIOScheduler(std::move(source));
    runtime.addIOScheduler(std::move(sibling));
    runtime.start();

    const bool ready = waitUntil([&]() {
        return source_ptr->threadId() != std::thread::id{} &&
               sibling_ptr->threadId() != std::thread::id{};
    });
    if (!ready) {
        std::cerr << "[T117] scheduler threads did not start in time\n";
        runtime.stop();
        return false;
    }

    ScenarioState state;
    state.source = source_ptr;
    state.sibling = sibling_ptr;
    for (int i = 0; i < kTaskCount; ++i) {
        if (!scheduleTask(*source_ptr, backlogTask(&state))) {
            std::cerr << "[T117] failed to enqueue skewed backlog task " << i << "\n";
            state.release.store(true, std::memory_order_release);
            runtime.stop();
            return false;
        }
    }

    const bool sibling_started = waitUntil([&]() {
        return state.ran_on_sibling.load(std::memory_order_acquire) > 0;
    });
    state.release.store(true, std::memory_order_release);
    const bool all_done = waitUntil([&]() {
        return state.completed.load(std::memory_order_acquire) == kTaskCount;
    }, 4000ms);

    runtime.stop();

    if (!sibling_started || !all_done) {
        std::cerr << "[T117] skewed backlog did not trigger stealing or did not complete in time\n";
        return false;
    }

    const auto stats = runtime.stats();
    if (stats.io_schedulers.size() != 2) {
        std::cerr << "[T117] expected two io scheduler stats, actual="
                  << stats.io_schedulers.size() << "\n";
        return false;
    }

    uint64_t total_attempts = 0;
    uint64_t total_successes = 0;
    for (const auto& scheduler : stats.io_schedulers) {
        total_attempts += scheduler.steal_attempts;
        total_successes += scheduler.steal_successes;
    }

    if (total_attempts == 0) {
        std::cerr << "[T117] expected at least one steal attempt in stats\n";
        return false;
    }

    if (total_successes == 0) {
        std::cerr << "[T117] expected at least one successful steal in stats\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!runStatsScenario()) {
        return 1;
    }

    std::cout << "T117-RuntimeIOStealStats PASS\n";
    return 0;
}
