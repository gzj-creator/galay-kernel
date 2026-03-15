/**
 * @file B14-scheduler_injected_wakeup.cc
 * @brief 用途：压测跨线程注入任务后的 scheduler 唤醒吞吐与延迟。
 * 关键覆盖点：多生产者远端注入、完成计数、延迟采样与唤醒收敛。
 * 通过条件：压测样本全部完成并输出结果，进程无崩溃、死锁或超时。
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "benchmark/BenchmarkSync.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "test/StdoutLog.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/IOUringScheduler.h"
#endif

using namespace galay::kernel;
using namespace std::chrono_literals;

namespace {

constexpr int kProducerCount = 4;
constexpr int kTasksPerProducer = 50000;
constexpr int kLatencySamples = 10000;
constexpr int kLatencyWarmupSamples = 2000;

struct BenchState {
    std::atomic<int64_t> completed{0};
    std::atomic<int64_t> latency_sum_ns{0};
    galay::benchmark::CompletionLatch* completion_latch = nullptr;
};

Coroutine throughputTask(BenchState* state) {
    state->completed.fetch_add(1, std::memory_order_relaxed);
    if (state->completion_latch) {
        state->completion_latch->arrive();
    }
    co_return;
}

Coroutine latencyTask(BenchState* state,
                      std::chrono::steady_clock::time_point submitted_at) {
    const auto now = std::chrono::steady_clock::now();
    const auto latency_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - submitted_at).count();
    state->latency_sum_ns.fetch_add(latency_ns, std::memory_order_relaxed);
    state->completed.fetch_add(1, std::memory_order_relaxed);
    if (state->completion_latch) {
        state->completion_latch->arrive();
    }
    co_return;
}

template <typename SchedulerT>
void runThroughputBenchmark() {
    SchedulerT scheduler;
    BenchState state;
    const int64_t total_tasks = static_cast<int64_t>(kProducerCount) * kTasksPerProducer;
    galay::benchmark::CompletionLatch completion_latch(static_cast<std::size_t>(total_tasks));
    state.completion_latch = &completion_latch;

    scheduler.start();
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);

    for (int producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&scheduler, &state]() {
            for (int i = 0; i < kTasksPerProducer; ++i) {
                scheduler.spawn(throughputTask(&state));
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    completion_latch.wait();

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double elapsed_ms = static_cast<double>(elapsed_ns) / 1'000'000.0;
    const double throughput =
        elapsed_ns > 0 ? (static_cast<double>(total_tasks) * 1'000'000'000.0 / elapsed_ns) : 0.0;

    LogInfo("[InjectedThroughput] producers={}, tasks_per_producer={}, total={}, time={}ms, throughput={:.0f} tasks/s",
            kProducerCount,
            kTasksPerProducer,
            total_tasks,
            elapsed_ms,
            throughput);
    scheduler.stop();
}

template <typename SchedulerT>
void runLatencyBenchmark() {
    SchedulerT scheduler;
    scheduler.start();

    for (int i = 0; i < kLatencyWarmupSamples; ++i) {
        BenchState warmup_state;
        galay::benchmark::CompletionLatch warmup_latch(1);
        warmup_state.completion_latch = &warmup_latch;
        scheduler.spawn(throughputTask(&warmup_state));
        warmup_latch.wait();
    }

    auto start = std::chrono::steady_clock::now();
    int64_t latency_sum_ns = 0;
    for (int i = 0; i < kLatencySamples; ++i) {
        BenchState state;
        galay::benchmark::CompletionLatch completion_latch(1);
        state.completion_latch = &completion_latch;
        scheduler.spawn(latencyTask(&state, std::chrono::steady_clock::now()));
        completion_latch.wait();
        latency_sum_ns += state.latency_sum_ns.load(std::memory_order_relaxed);
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double elapsed_ms = static_cast<double>(elapsed_ns) / 1'000'000.0;
    const double avg_latency_us =
        static_cast<double>(latency_sum_ns) /
        static_cast<double>(kLatencySamples) / 1000.0;

    LogInfo("[InjectedLatency] samples={}, time={}ms, avg_latency={:.2f}us",
            kLatencySamples,
            elapsed_ms,
            avg_latency_us);
    scheduler.stop();
}

}  // namespace

int main() {
#if defined(USE_KQUEUE)
    KqueueScheduler scheduler;
    constexpr const char* backend = "kqueue";
#elif defined(USE_EPOLL)
    EpollScheduler scheduler;
    constexpr const char* backend = "epoll";
#elif defined(USE_IOURING)
    IOUringScheduler scheduler;
    constexpr const char* backend = "io_uring";
#else
    std::cout << "B14-SchedulerInjectedWakeup SKIP\n";
    return 0;
#endif

    LogInfo("Scheduler injected wakeup benchmark, backend={}", backend);
    (void)scheduler;

    runThroughputBenchmark<std::decay_t<decltype(scheduler)>>();
    std::this_thread::sleep_for(50ms);
    runLatencyBenchmark<std::decay_t<decltype(scheduler)>>();
    return 0;
}
