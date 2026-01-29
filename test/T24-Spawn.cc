#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

using namespace galay::kernel;

// 用于验证协程执行的计数器
std::atomic<int> child_count{0};
std::atomic<int> parent_count{0};

// 简单的子协程
Coroutine childTask(int id)
{
    std::cout << "Child task " << id << " started on thread "
              << std::this_thread::get_id() << std::endl;
    child_count++;
    co_return;
}

// 测试1: 基本的spawn功能
Coroutine testBasicSpawn()
{
    std::cout << "\n=== Test 1: Basic Spawn ===" << std::endl;
    std::cout << "Parent task started" << std::endl;

    // 使用 spawn 启动多个子协程
    co_await spawn(childTask(1));
    co_await spawn(childTask(2));
    co_await spawn(childTask(3));

    std::cout << "Parent task finished spawning children" << std::endl;
    parent_count++;
    co_return;
}

// 嵌套的子协程
Coroutine nestedChild(int parent_id, int child_id)
{
    std::cout << "  Nested child " << parent_id << "-" << child_id << " started" << std::endl;
    child_count++;
    co_return;
}

Coroutine nestedParent(int id)
{
    std::cout << "Nested parent " << id << " started" << std::endl;

    // 在子协程中再spawn更多协程
    co_await spawn(nestedChild(id, 1));
    co_await spawn(nestedChild(id, 2));

    std::cout << "Nested parent " << id << " finished" << std::endl;
    child_count++;
    co_return;
}

// 测试2: 嵌套spawn
Coroutine testNestedSpawn()
{
    std::cout << "\n=== Test 2: Nested Spawn ===" << std::endl;
    std::cout << "Root task started" << std::endl;

    // spawn多个父协程，每个父协程又会spawn子协程
    co_await spawn(nestedParent(1));
    co_await spawn(nestedParent(2));

    std::cout << "Root task finished" << std::endl;
    parent_count++;
    co_return;
}

// 带延迟的子协程
Coroutine delayedChild(int id, int delay_ms)
{
    std::cout << "Delayed child " << id << " started (delay: " << delay_ms << "ms)" << std::endl;

    // 模拟一些工作
    for(int i = 0; i < delay_ms / 10; i++) {
        co_yield true; // 让出执行权
    }

    std::cout << "Delayed child " << id << " finished" << std::endl;
    child_count++;
    co_return;
}

// 测试3: spawn不会阻塞父协程
Coroutine testNonBlocking()
{
    std::cout << "\n=== Test 3: Non-blocking Spawn ===" << std::endl;
    std::cout << "Parent started" << std::endl;

    // spawn多个带延迟的子协程
    co_await spawn(delayedChild(1, 50));
    std::cout << "After spawn 1 (parent continues immediately)" << std::endl;

    co_await spawn(delayedChild(2, 30));
    std::cout << "After spawn 2 (parent continues immediately)" << std::endl;

    co_await spawn(delayedChild(3, 20));
    std::cout << "After spawn 3 (parent continues immediately)" << std::endl;

    std::cout << "Parent finished (children may still be running)" << std::endl;
    parent_count++;
    co_return;
}

// 递归spawn测试
Coroutine recursiveTask(int depth, int max_depth)
{
    std::cout << "Recursive task at depth " << depth << std::endl;

    if (depth < max_depth) {
        // 递归spawn更深层的协程
        co_await spawn(recursiveTask(depth + 1, max_depth));
    }

    std::cout << "Recursive task at depth " << depth << " finished" << std::endl;
    child_count++;
    co_return;
}

// 测试4: 递归spawn
Coroutine testRecursiveSpawn()
{
    std::cout << "\n=== Test 4: Recursive Spawn ===" << std::endl;
    co_await spawn(recursiveTask(0, 5));
    std::cout << "Recursive spawn test finished" << std::endl;
    parent_count++;
    co_return;
}

// 大量spawn测试
Coroutine massChild(int id)
{
    child_count++;
    co_return;
}

Coroutine testMassSpawn()
{
    std::cout << "\n=== Test 5: Mass Spawn ===" << std::endl;
    std::cout << "Spawning 100 child coroutines..." << std::endl;

    for (int i = 0; i < 100; i++) {
        co_await spawn(massChild(i));
    }

    std::cout << "Mass spawn finished" << std::endl;
    parent_count++;
    co_return;
}

// 主测试协程
Coroutine runAllTests()
{
    std::cout << "Starting spawn function tests..." << std::endl;

    // 测试1: 基本spawn
    child_count = 0;
    parent_count = 0;
    co_await testBasicSpawn().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Test 1 result: parent_count=" << parent_count
              << ", child_count=" << child_count << std::endl;

    // 测试2: 嵌套spawn
    child_count = 0;
    parent_count = 0;
    co_await testNestedSpawn().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Test 2 result: parent_count=" << parent_count
              << ", child_count=" << child_count << std::endl;

    // 测试3: 非阻塞spawn
    child_count = 0;
    parent_count = 0;
    co_await testNonBlocking().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Test 3 result: parent_count=" << parent_count
              << ", child_count=" << child_count << std::endl;

    // 测试4: 递归spawn
    child_count = 0;
    parent_count = 0;
    co_await testRecursiveSpawn().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Test 4 result: parent_count=" << parent_count
              << ", child_count=" << child_count << std::endl;

    // 测试5: 大量spawn
    child_count = 0;
    parent_count = 0;
    co_await testMassSpawn().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Test 5 result: parent_count=" << parent_count
              << ", child_count=" << child_count << std::endl;

    std::cout << "\n=== All tests completed ===" << std::endl;
    co_return;
}

int main()
{
    Runtime runtime;
    runtime.start();

    auto scheduler = runtime.getNextComputeScheduler();
    scheduler->spawn(runAllTests());

    // 等待所有测试完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    runtime.stop();

    std::cout << "\nTest program finished" << std::endl;
    return 0;
}
