/**
 * @file test_runtime.cc
 * @brief Runtime 类测试 - 管理多个 IO 和计算调度器
 */

#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/concurrency/AsyncWaiter.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"
#include "test_result_writer.h"

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
using IOSchedulerType = galay::kernel::EpollScheduler;
#elif defined(USE_KQUEUE)
#include "galay-kernel/kernel/KqueueScheduler.h"
using IOSchedulerType = galay::kernel::KqueueScheduler;
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/IOUringScheduler.h"
using IOSchedulerType = galay::kernel::IOUringScheduler;
#endif

using namespace galay::kernel;
using namespace std::chrono_literals;

std::atomic<int> g_passed{0};
std::atomic<int> g_total{0};

// ============== 测试1: Runtime 基本功能 ==============
void test_runtime_basic() {
    ++g_total;
    std::cout << "\n[测试1] Runtime 基本功能测试" << std::endl;

    Runtime runtime;

    // 添加 2 个 IO 调度器
    auto io1 = std::make_unique<IOSchedulerType>();
    auto io2 = std::make_unique<IOSchedulerType>();

    bool ret1 = runtime.addIOScheduler(std::move(io1));
    bool ret2 = runtime.addIOScheduler(std::move(io2));

    // 添加 2 个计算调度器
    auto compute1 = std::make_unique<ComputeScheduler>();
    auto compute2 = std::make_unique<ComputeScheduler>();

    bool ret3 = runtime.addComputeScheduler(std::move(compute1));
    bool ret4 = runtime.addComputeScheduler(std::move(compute2));

    if (!ret1 || !ret2 || !ret3 || !ret4) {
        std::cout << "❌ 添加调度器失败" << std::endl;
        return;
    }

    // 检查调度器数量
    if (runtime.getIOSchedulerCount() != 2 || runtime.getComputeSchedulerCount() != 2) {
        std::cout << "❌ 调度器数量不正确" << std::endl;
        return;
    }

    // 启动 Runtime
    runtime.start();

    if (!runtime.isRunning()) {
        std::cout << "❌ Runtime 未启动" << std::endl;
        return;
    }

    // 获取调度器
    auto* io_scheduler = runtime.getIOScheduler(0);
    auto* compute_scheduler = runtime.getComputeScheduler(0);

    if (!io_scheduler || !compute_scheduler) {
        std::cout << "❌ 获取调度器失败" << std::endl;
        return;
    }

    std::this_thread::sleep_for(100ms);

    // 停止 Runtime
    runtime.stop();

    if (runtime.isRunning()) {
        std::cout << "❌ Runtime 未停止" << std::endl;
        return;
    }

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 测试2: Runtime 启动后不允许添加调度器 ==============
void test_runtime_no_add_after_start() {
    ++g_total;
    std::cout << "\n[测试2] Runtime 启动后不允许添加调度器" << std::endl;

    Runtime runtime;

    auto io1 = std::make_unique<IOSchedulerType>();
    runtime.addIOScheduler(std::move(io1));

    runtime.start();

    // 尝试在启动后添加调度器
    auto io2 = std::make_unique<IOSchedulerType>();
    bool ret = runtime.addIOScheduler(std::move(io2));

    runtime.stop();

    if (ret) {
        std::cout << "❌ 启动后不应允许添加调度器" << std::endl;
        return;
    }

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 测试3: 轮询获取调度器（负载均衡） ==============
void test_runtime_round_robin() {
    ++g_total;
    std::cout << "\n[测试3] 轮询获取调度器（负载均衡）" << std::endl;

    Runtime runtime;

    // 添加 3 个 IO 调度器
    for (int i = 0; i < 3; ++i) {
        auto io = std::make_unique<IOSchedulerType>();
        runtime.addIOScheduler(std::move(io));
    }

    // 添加 2 个计算调度器
    for (int i = 0; i < 2; ++i) {
        auto compute = std::make_unique<ComputeScheduler>();
        runtime.addComputeScheduler(std::move(compute));
    }

    runtime.start();

    // 测试轮询获取 IO 调度器
    auto* io1 = runtime.getNextIOScheduler();
    auto* io2 = runtime.getNextIOScheduler();
    auto* io3 = runtime.getNextIOScheduler();
    auto* io4 = runtime.getNextIOScheduler();  // 应该回到第一个

    if (!io1 || !io2 || !io3 || !io4) {
        std::cout << "❌ 获取 IO 调度器失败" << std::endl;
        runtime.stop();
        return;
    }

    if (io1 != io4) {
        std::cout << "❌ IO 调度器轮询不正确" << std::endl;
        runtime.stop();
        return;
    }

    // 测试轮询获取计算调度器
    auto* compute1 = runtime.getNextComputeScheduler();
    auto* compute2 = runtime.getNextComputeScheduler();
    auto* compute3 = runtime.getNextComputeScheduler();  // 应该回到第一个

    if (!compute1 || !compute2 || !compute3) {
        std::cout << "❌ 获取计算调度器失败" << std::endl;
        runtime.stop();
        return;
    }

    if (compute1 != compute3) {
        std::cout << "❌ 计算调度器轮询不正确" << std::endl;
        runtime.stop();
        return;
    }

    runtime.stop();

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 测试4: Runtime 管理多个调度器执行任务 ==============
std::atomic<int> g_test4_io_count{0};
std::atomic<int> g_test4_compute_count{0};

Coroutine computeTask(AsyncWaiter<int>* waiter, int value) {
    // 模拟计算任务
    volatile int sum = 0;
    for (int i = 0; i < 10000; ++i) {
        sum += (i + value) % 100;
    }
    g_test4_compute_count.fetch_add(1, std::memory_order_relaxed);
    waiter->notify(sum);
    co_return;
}

Coroutine ioTask(Runtime* runtime, int id) {
    // 获取计算调度器（轮询方式）
    auto* compute_scheduler = runtime->getNextComputeScheduler();
    if (!compute_scheduler) {
        co_return;
    }

    // 提交计算任务
    AsyncWaiter<int> waiter;
    compute_scheduler->spawn(computeTask(&waiter, id));

    // 等待计算完成
    auto result = co_await waiter.wait();
    (void)result;

    g_test4_io_count.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

void test_runtime_with_tasks() {
    ++g_total;
    std::cout << "\n[测试4] Runtime 管理多个调度器执行任务" << std::endl;

    g_test4_io_count.store(0);
    g_test4_compute_count.store(0);

    Runtime runtime;

    // 添加 2 个 IO 调度器
    for (int i = 0; i < 2; ++i) {
        auto io = std::make_unique<IOSchedulerType>();
        runtime.addIOScheduler(std::move(io));
    }

    // 添加 3 个计算调度器
    for (int i = 0; i < 3; ++i) {
        auto compute = std::make_unique<ComputeScheduler>();
        runtime.addComputeScheduler(std::move(compute));
    }

    runtime.start();

    // 提交 10 个 IO 任务到不同的 IO 调度器
    constexpr int TASK_COUNT = 10;
    for (int i = 0; i < TASK_COUNT; ++i) {
        auto* io_scheduler = runtime.getNextIOScheduler();
        io_scheduler->spawn(ioTask(&runtime, i));
    }

    // 等待任务完成
    for (int i = 0; i < 50; ++i) {
        if (g_test4_io_count.load() == TASK_COUNT &&
            g_test4_compute_count.load() == TASK_COUNT) {
            break;
        }
        std::this_thread::sleep_for(100ms);
    }

    runtime.stop();

    int io_count = g_test4_io_count.load();
    int compute_count = g_test4_compute_count.load();

    if (io_count != TASK_COUNT || compute_count != TASK_COUNT) {
        std::cout << "❌ 任务执行不完整: IO=" << io_count
                  << ", Compute=" << compute_count << std::endl;
        return;
    }

    std::cout << "✅ 测试通过 (IO任务: " << io_count
              << ", 计算任务: " << compute_count << ")" << std::endl;
    ++g_passed;
}

// ============== 测试5: 自动配置（零配置启动） ==============
void test_runtime_auto_config() {
    ++g_total;
    std::cout << "\n[测试5] 自动配置（零配置启动）" << std::endl;

    Runtime runtime;  // 不手动添加任何调度器

    runtime.start();  // 应该自动创建默认数量的调度器

    // 检查是否自动创建了调度器
    size_t io_count = runtime.getIOSchedulerCount();
    size_t compute_count = runtime.getComputeSchedulerCount();

    std::cout << "  自动创建: " << io_count << " 个 IO 调度器, "
              << compute_count << " 个计算调度器" << std::endl;

    if (io_count == 0 || compute_count == 0) {
        std::cout << "❌ 自动配置失败，调度器数量为 0" << std::endl;
        runtime.stop();
        return;
    }

    // 测试能否正常获取调度器
    auto* io = runtime.getNextIOScheduler();
    auto* compute = runtime.getNextComputeScheduler();

    if (!io || !compute) {
        std::cout << "❌ 无法获取自动创建的调度器" << std::endl;
        runtime.stop();
        return;
    }

    runtime.stop();

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 测试6: 指定调度器数量 ==============
void test_runtime_specified_count() {
    ++g_total;
    std::cout << "\n[测试6] 指定调度器数量" << std::endl;

    // 指定创建 3 个 IO 调度器和 5 个计算调度器
    Runtime runtime(LoadBalanceStrategy::ROUND_ROBIN, 3, 5);

    runtime.start();

    size_t io_count = runtime.getIOSchedulerCount();
    size_t compute_count = runtime.getComputeSchedulerCount();

    std::cout << "  创建: " << io_count << " 个 IO 调度器, "
              << compute_count << " 个计算调度器" << std::endl;

    runtime.stop();

    if (io_count != 3 || compute_count != 5) {
        std::cout << "❌ 调度器数量不符合预期" << std::endl;
        return;
    }

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 测试7: 手动添加优先于自动配置 ==============
void test_runtime_manual_priority() {
    ++g_total;
    std::cout << "\n[测试7] 手动添加优先于自动配置" << std::endl;

    // 指定自动创建数量，但手动添加调度器
    Runtime runtime(LoadBalanceStrategy::ROUND_ROBIN, 10, 10);

    // 手动添加 2 个 IO 调度器
    auto io1 = std::make_unique<IOSchedulerType>();
    auto io2 = std::make_unique<IOSchedulerType>();
    runtime.addIOScheduler(std::move(io1));
    runtime.addIOScheduler(std::move(io2));

    runtime.start();

    size_t io_count = runtime.getIOSchedulerCount();
    size_t compute_count = runtime.getComputeSchedulerCount();

    std::cout << "  实际创建: " << io_count << " 个 IO 调度器, "
              << compute_count << " 个计算调度器" << std::endl;

    runtime.stop();

    // 手动添加的调度器应该被使用，不会自动创建
    if (io_count != 2 || compute_count != 0) {
        std::cout << "❌ 手动添加后不应自动创建调度器" << std::endl;
        return;
    }

    std::cout << "✅ 测试通过" << std::endl;
    ++g_passed;
}

// ============== 主函数 ==============
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Runtime 类测试" << std::endl;
    std::cout << "========================================" << std::endl;

    test_runtime_basic();
    test_runtime_no_add_after_start();
    test_runtime_round_robin();
    test_runtime_with_tasks();
    test_runtime_auto_config();
    test_runtime_specified_count();
    test_runtime_manual_priority();

    std::cout << "\n========================================" << std::endl;
    std::cout << "测试结果: " << g_passed.load() << "/" << g_total.load() << " 通过" << std::endl;
    std::cout << "========================================" << std::endl;

    // 写入测试结果
    galay::test::TestResultWriter writer("test_runtime");
    for (int i = 0; i < g_total.load(); ++i) {
        writer.addTest();
    }
    for (int i = 0; i < g_passed.load(); ++i) {
        writer.addPassed();
    }
    for (int i = 0; i < (g_total.load() - g_passed.load()); ++i) {
        writer.addFailed();
    }
    writer.writeResult();

    return (g_passed.load() == g_total.load()) ? 0 : 1;
}