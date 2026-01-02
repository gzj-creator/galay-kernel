/**
 * @file bench_compute_scheduler.cc
 * @brief ComputeScheduler 性能压测
 *
 * 测试项目：
 * 1. 吞吐量：每秒可处理的协程数
 * 2. 延迟：协程从提交到执行的延迟
 * 3. 扩展性：不同线程数下的性能
 */

#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>
#include <cmath>
#include <iomanip>
#include <thread>
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

using namespace galay::kernel;
using namespace std::chrono_literals;

// ============== 压测参数 ==============
constexpr int WARMUP_COUNT = 1000;           // 预热任务数
constexpr int THROUGHPUT_TASKS = 100000;     // 吞吐量测试任务数
constexpr int LATENCY_TASKS = 10000;         // 延迟测试任务数
constexpr int COMPUTE_ITERATIONS = 1000;     // 计算密集型迭代次数

// ============== 全局计数器 ==============
std::atomic<int64_t> g_completed{0};
std::atomic<int64_t> g_latency_sum{0};
std::atomic<int64_t> g_latency_count{0};

// ============== 测试协程 ==============

// 空协程（测试调度开销）
Coroutine emptyTask() {
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// 轻量计算协程
Coroutine lightComputeTask() {
    volatile int sum = 0;
    for (int i = 0; i < 100; ++i) {
        sum += i;
    }
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// 计算密集型协程
Coroutine heavyComputeTask() {
    volatile double result = 0;
    for (int i = 0; i < COMPUTE_ITERATIONS; ++i) {
        result += std::sin(i) * std::cos(i);
    }
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// 延迟测试协程
struct LatencyTask {
    std::chrono::steady_clock::time_point submit_time;
};

std::vector<LatencyTask> g_latency_tasks;
std::mutex g_latency_mutex;

Coroutine latencyTask(int index) {
    auto now = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - g_latency_tasks[index].submit_time).count();
    g_latency_sum.fetch_add(latency, std::memory_order_relaxed);
    g_latency_count.fetch_add(1, std::memory_order_relaxed);
    g_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============== 压测函数 ==============

void resetCounters() {
    g_completed = 0;
    g_latency_sum = 0;
    g_latency_count = 0;
}

// 吞吐量测试
void benchThroughput(const std::string& name, int thread_count, int task_count,
                     std::function<Coroutine()> task_factory) {
    resetCounters();

    ComputeScheduler scheduler(thread_count);
    scheduler.start();

    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        scheduler.spawn(task_factory());
    }
    while (g_completed < WARMUP_COUNT) {
        std::this_thread::sleep_for(1ms);
    }
    resetCounters();

    // 正式测试
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < task_count; ++i) {
        scheduler.spawn(task_factory());
    }

    // 等待所有任务完成
    while (g_completed < task_count) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)task_count / ms * 1000.0;

    scheduler.stop();

    LogInfo("[{}] threads={}, tasks={}, time={}ms, throughput={:.0f} tasks/sec",
            name, thread_count, task_count, ms, throughput);
}

// 延迟测试
void benchLatency(int thread_count, int task_count) {
    resetCounters();
    g_latency_tasks.clear();
    g_latency_tasks.resize(task_count);

    ComputeScheduler scheduler(thread_count);
    scheduler.start();

    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        scheduler.spawn(emptyTask());
    }
    while (g_completed < WARMUP_COUNT) {
        std::this_thread::sleep_for(1ms);
    }
    resetCounters();

    // 正式测试
    for (int i = 0; i < task_count; ++i) {
        g_latency_tasks[i].submit_time = std::chrono::steady_clock::now();
        scheduler.spawn(latencyTask(i));
    }

    // 等待所有任务完成
    while (g_completed < task_count) {
        std::this_thread::sleep_for(1ms);
    }

    scheduler.stop();

    double avg_latency_us = (double)g_latency_sum / g_latency_count / 1000.0;

    LogInfo("[Latency] threads={}, tasks={}, avg_latency={:.2f}us",
            thread_count, task_count, avg_latency_us);
}

// 扩展性测试
void benchScalability() {
    LogInfo("--- Scalability Test (heavy compute tasks) ---");

    std::vector<int> thread_counts = {1, 2, 4, 8};
    int task_count = 1000;

    double baseline_throughput = 0;

    for (int threads : thread_counts) {
        resetCounters();

        ComputeScheduler scheduler(threads);
        scheduler.start();

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < task_count; ++i) {
            scheduler.spawn(heavyComputeTask());
        }

        while (g_completed < task_count) {
            std::this_thread::sleep_for(1ms);
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        double throughput = (double)task_count / ms * 1000.0;

        scheduler.stop();

        if (threads == 1) {
            baseline_throughput = throughput;
        }

        double speedup = throughput / baseline_throughput;

        LogInfo("  threads={}: time={}ms, throughput={:.0f}/s, speedup={:.2f}x",
                threads, ms, throughput, speedup);
    }
}

// 持续压力测试
void benchSustained(int thread_count, int duration_sec) {
    LogInfo("--- Sustained Load Test ({}s) ---", duration_sec);

    resetCounters();

    ComputeScheduler scheduler(thread_count);
    scheduler.start();

    auto start = std::chrono::steady_clock::now();
    auto end_time = start + std::chrono::seconds(duration_sec);

    std::atomic<bool> running{true};

    // 生产者线程
    std::thread producer([&]() {
        while (running) {
            scheduler.spawn(lightComputeTask());
            // 控制提交速率
            if (g_completed < 10000) {
                continue;
            }
            std::this_thread::sleep_for(1us);
        }
    });

    // 监控线程
    int64_t last_completed = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(1s);
        int64_t current = g_completed.load();
        int64_t delta = current - last_completed;
        LogInfo("  throughput: {}/s, total: {}", delta, current);
        last_completed = current;
    }

    running = false;
    producer.join();

    // 等待剩余任务完成
    std::this_thread::sleep_for(100ms);
    scheduler.stop();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double avg_throughput = (double)g_completed / ms * 1000.0;

    LogInfo("  total: {} tasks in {}ms, avg throughput: {:.0f}/s",
            g_completed.load(), ms, avg_throughput);
}

int main(int argc, char* argv[]) {
    int thread_count = std::thread::hardware_concurrency();
    if (argc > 1) {
        thread_count = std::atoi(argv[1]);
    }

    LogInfo("=== ComputeScheduler Benchmark ===");
    LogInfo("CPU cores: {}, using {} threads", std::thread::hardware_concurrency(), thread_count);
    LogInfo("");

    // 1. 吞吐量测试 - 空任务
    LogInfo("--- Throughput Test (empty tasks) ---");
    benchThroughput("Empty", thread_count, THROUGHPUT_TASKS, emptyTask);

    LogInfo("");

    // 2. 吞吐量测试 - 轻量计算
    LogInfo("--- Throughput Test (light compute) ---");
    benchThroughput("Light", thread_count, THROUGHPUT_TASKS, lightComputeTask);

    LogInfo("");

    // 3. 吞吐量测试 - 重计算
    LogInfo("--- Throughput Test (heavy compute) ---");
    benchThroughput("Heavy", thread_count, 10000, heavyComputeTask);

    LogInfo("");

    // 4. 延迟测试
    LogInfo("--- Latency Test ---");
    benchLatency(thread_count, LATENCY_TASKS);

    LogInfo("");

    // 5. 扩展性测试
    benchScalability();

    LogInfo("");

    // 6. 持续压力测试
    benchSustained(thread_count, 5);

    LogInfo("");
    LogInfo("=== Benchmark Complete ===");

    return 0;
}
