#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

using namespace galay::kernel;

std::atomic<int> task_counter{0};

// 工作协程
Coroutine worker(int id)
{
    std::cout << "[Worker " << id << "] Started on thread "
              << std::this_thread::get_id() << std::endl;

    // 模拟一些工作
    for(int i = 0; i < 3; i++) {
        co_yield true;  // 让出执行权
    }

    task_counter++;
    std::cout << "[Worker " << id << "] Completed (total: "
              << task_counter.load() << ")" << std::endl;
    co_return;
}

// 主协程 - 使用 spawn 启动多个工作协程
Coroutine mainTask()
{
    std::cout << "\n=== Main Task Started ===" << std::endl;
    std::cout << "Spawning 5 worker coroutines using spawn()...\n" << std::endl;

    // 使用 spawn 启动多个工作协程
    co_await spawn(worker(1));
    co_await spawn(worker(2));
    co_await spawn(worker(3));
    co_await spawn(worker(4));
    co_await spawn(worker(5));

    std::cout << "\n=== Main Task: All workers spawned ===" << std::endl;
    std::cout << "Main task continues without waiting for workers to complete\n" << std::endl;

    // 主任务继续执行自己的工作
    for(int i = 0; i < 5; i++) {
        std::cout << "[Main] Working... step " << (i+1) << std::endl;
        co_yield true;
    }

    std::cout << "\n=== Main Task Completed ===" << std::endl;
    co_return;
}

// 演示 spawn vs wait 的区别
Coroutine slowTask(int id)
{
    std::cout << "[Slow Task " << id << "] Started" << std::endl;
    for(int i = 0; i < 10; i++) {
        co_yield true;
    }
    std::cout << "[Slow Task " << id << "] Completed" << std::endl;
    co_return;
}

Coroutine compareSpawnVsWait()
{
    std::cout << "\n\n=== Comparing spawn() vs wait() ===" << std::endl;

    auto start = std::chrono::steady_clock::now();

    // 使用 spawn - 不等待
    std::cout << "\n[Using spawn] Starting tasks..." << std::endl;
    co_await spawn(slowTask(1));
    co_await spawn(slowTask(2));
    co_await spawn(slowTask(3));

    auto spawn_time = std::chrono::steady_clock::now();
    auto spawn_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        spawn_time - start).count();

    std::cout << "[Using spawn] All tasks spawned in " << spawn_duration
              << "ms (non-blocking)" << std::endl;

    // 让任务有时间执行
    for(int i = 0; i < 15; i++) {
        co_yield true;
    }

    std::cout << "\n[Using wait] Starting tasks..." << std::endl;
    start = std::chrono::steady_clock::now();

    // 使用 wait - 等待每个任务完成
    co_await slowTask(4).wait();
    co_await slowTask(5).wait();
    co_await slowTask(6).wait();

    auto wait_time = std::chrono::steady_clock::now();
    auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        wait_time - start).count();

    std::cout << "[Using wait] All tasks completed in " << wait_duration
              << "ms (blocking)" << std::endl;

    std::cout << "\n=== Comparison Complete ===" << std::endl;
    co_return;
}

// 嵌套 spawn 示例
Coroutine leafTask(int parent_id, int task_id)
{
    std::cout << "  [Leaf " << parent_id << "-" << task_id << "] Executing" << std::endl;
    co_yield true;
    co_return;
}

Coroutine branchTask(int id)
{
    std::cout << "[Branch " << id << "] Started, spawning leaf tasks..." << std::endl;

    // 在子协程中继续 spawn 更多协程
    co_await spawn(leafTask(id, 1));
    co_await spawn(leafTask(id, 2));
    co_await spawn(leafTask(id, 3));

    std::cout << "[Branch " << id << "] Completed" << std::endl;
    co_return;
}

Coroutine nestedSpawnDemo()
{
    std::cout << "\n\n=== Nested Spawn Demo ===" << std::endl;
    std::cout << "Root spawns branches, branches spawn leaves\n" << std::endl;

    co_await spawn(branchTask(1));
    co_await spawn(branchTask(2));

    std::cout << "\n[Root] All branches spawned" << std::endl;

    // 让任务执行
    for(int i = 0; i < 10; i++) {
        co_yield true;
    }

    std::cout << "\n=== Nested Spawn Demo Complete ===" << std::endl;
    co_return;
}

// 运行所有演示
Coroutine runAllDemos()
{
    // Demo 1: 基本 spawn 用法
    task_counter = 0;
    co_await mainTask().wait();

    // 等待工作协程完成
    for(int i = 0; i < 20; i++) {
        co_yield true;
    }

    std::cout << "\nFinal worker count: " << task_counter.load() << "/5" << std::endl;

    // Demo 2: spawn vs wait 对比
    co_await compareSpawnVsWait().wait();

    // Demo 3: 嵌套 spawn
    co_await nestedSpawnDemo().wait();

    std::cout << "\n\n=== All Demos Completed ===" << std::endl;
    co_return;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  Spawn Function Demonstration" << std::endl;
    std::cout << "========================================" << std::endl;

    Runtime runtime;
    runtime.start();

    auto scheduler = runtime.getNextComputeScheduler();
    scheduler->spawn(runAllDemos());

    // 等待所有演示完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    runtime.stop();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Program Finished" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
