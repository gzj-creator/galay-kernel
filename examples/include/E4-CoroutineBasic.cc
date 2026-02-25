/**
 * @file E4-CoroutineBasic.cc
 * @brief 协程基础示例
 * @details 演示协程的基本用法，包括创建、spawn和等待
 *
 * 使用场景：
 *   - 学习协程基本概念
 *   - 理解协程的创建和调度
 *   - 掌握协程间的等待和同步
 */

#include <iostream>
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/common/Log.h"

using namespace galay::kernel;

/**
 * @brief 简单的计算协程
 */
Coroutine simpleTask(int id) {
    LogInfo("Task {} started", id);

    // 模拟一些计算工作
    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }

    LogInfo("Task {} completed, sum = {}", id, sum);
    co_return;
}

/**
 * @brief 父协程，spawn多个子协程
 */
Coroutine parentTask() {
    LogInfo("Parent task started");

    // Spawn多个子协程
    for (int i = 0; i < 3; ++i) {
        co_await spawn(simpleTask(i));
    }

    LogInfo("Parent task: all child tasks spawned");
    co_return;
}

/**
 * @brief 演示协程等待
 */
Coroutine waitExample() {
    LogInfo("Wait example started");

    // 创建一个协程
    Coroutine task = simpleTask(100);

    // 等待协程完成
    co_await task.wait();

    LogInfo("Wait example: task completed");
    co_return;
}

int main() {
    LogInfo("=== Coroutine Basic Example ===");

    // 创建计算调度器
    ComputeScheduler scheduler;

    // 启动调度器
    scheduler.start();

    // 示例1: 简单协程
    LogInfo("\n--- Example 1: Simple Coroutine ---");
    scheduler.spawn(simpleTask(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 示例2: 父子协程
    LogInfo("\n--- Example 2: Parent-Child Coroutines ---");
    scheduler.spawn(parentTask());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 示例3: 协程等待
    LogInfo("\n--- Example 3: Coroutine Wait ---");
    scheduler.spawn(waitExample());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 停止调度器
    scheduler.stop();

    LogInfo("\n=== Example Completed ===");
    return 0;
}
