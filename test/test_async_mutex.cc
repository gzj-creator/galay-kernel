/**
 * @file test_async_mutex.cc
 * @brief AsyncMutex 单元测试
 */

#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "galay-kernel/concurrency/AsyncMutex.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

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

// ============================================================================
// 测试1：基本 lock/unlock
// ============================================================================
std::atomic<bool> g_test1_done{false};

Coroutine testBasicLockUnlock(AsyncMutex* mutex) {
    co_await mutex->lock();
    // 模拟临界区操作
    std::this_thread::sleep_for(10ms);
    mutex->unlock();
    g_test1_done = true;
    co_return;
}

// ============================================================================
// 测试2：tryLock
// ============================================================================
std::atomic<bool> g_test2_try_failed{false};
std::atomic<bool> g_test2_done{false};

Coroutine testTryLock(AsyncMutex* mutex) {
    // 先获取锁
    co_await mutex->lock();

    // 在持有锁时 tryLock 应该失败
    g_test2_try_failed = !mutex->tryLock();

    mutex->unlock();
    g_test2_done = true;
    co_return;
}

// ============================================================================
// 测试3：多协程竞争（互斥性验证）
// ============================================================================
std::atomic<int> g_test3_counter{0};
std::atomic<int> g_test3_max_concurrent{0};
std::atomic<int> g_test3_current{0};
std::atomic<int> g_test3_completed{0};
constexpr int TEST3_COROUTINE_COUNT = 10;

Coroutine testMutualExclusion(AsyncMutex* mutex) {
    co_await mutex->lock();

    int current = g_test3_current.fetch_add(1, std::memory_order_relaxed) + 1;

    // 记录最大并发数（应该始终为1）
    int max = g_test3_max_concurrent.load(std::memory_order_relaxed);
    while (current > max && !g_test3_max_concurrent.compare_exchange_weak(max, current));

    // 模拟临界区操作
    g_test3_counter++;
    std::this_thread::sleep_for(1ms);

    g_test3_current.fetch_sub(1, std::memory_order_relaxed);

    mutex->unlock();
    g_test3_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试4：公平性测试（FIFO顺序）
// ============================================================================
std::vector<int> g_test4_order;
std::mutex g_test4_order_mutex;
std::atomic<int> g_test4_completed{0};
constexpr int TEST4_COROUTINE_COUNT = 5;

Coroutine testFairness(AsyncMutex* mutex, int id) {
    co_await mutex->lock();

    {
        std::lock_guard<std::mutex> lock(g_test4_order_mutex);
        g_test4_order.push_back(id);
    }

    // 短暂持有锁
    std::this_thread::sleep_for(5ms);

    mutex->unlock();
    g_test4_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试5：压力测试
// ============================================================================
std::atomic<int> g_test5_sum{0};
std::atomic<int> g_test5_completed{0};
constexpr int TEST5_COROUTINE_COUNT = 50;
constexpr int TEST5_INCREMENT_COUNT = 10;

Coroutine testStress(AsyncMutex* mutex) {
    for (int i = 0; i < TEST5_INCREMENT_COUNT; ++i) {
        co_await mutex->lock();
        g_test5_sum++;
        mutex->unlock();
    }
    g_test5_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试6：isLocked 状态检查
// ============================================================================
std::atomic<bool> g_test6_locked_inside{false};
std::atomic<bool> g_test6_done{false};

Coroutine testIsLocked(AsyncMutex* mutex) {
    co_await mutex->lock();
    g_test6_locked_inside = mutex->isLocked();
    mutex->unlock();
    g_test6_done = true;
    co_return;
}

// ============================================================================
// 测试7：waiterCount 检查（简化版）
// ============================================================================
std::atomic<int> g_test7_waiterCount{0};
std::atomic<bool> g_test7_done{false};

Coroutine testWaiterCountSingle(AsyncMutex* mutex) {
    // 先获取锁
    co_await mutex->lock();
    // 初始 waiterCount 应该为 0
    g_test7_waiterCount = mutex->waiterCount();
    mutex->unlock();
    g_test7_done = true;
    co_return;
}

// ============================================================================
// 测试8：竞态条件测试（高并发）
// ============================================================================
std::atomic<int> g_test8_race_counter{0};
std::atomic<int> g_test8_completed{0};
constexpr int TEST8_COROUTINE_COUNT = 100;
constexpr int TEST8_INCREMENT_COUNT = 5;

Coroutine testRaceCondition(AsyncMutex* mutex) {
    for (int i = 0; i < TEST8_INCREMENT_COUNT; ++i) {
        co_await mutex->lock();
        // 非原子操作，如果互斥失效会导致数据竞争
        int val = g_test8_race_counter.load(std::memory_order_relaxed);
        g_test8_race_counter.store(val + 1, std::memory_order_relaxed);
        mutex->unlock();
    }
    g_test8_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试9：unlock 后立即 lock（快速切换）
// ============================================================================
std::atomic<int> g_test9_completed{0};
constexpr int TEST9_ITERATIONS = 100;

Coroutine testRapidLockUnlock(AsyncMutex* mutex) {
    for (int i = 0; i < TEST9_ITERATIONS; ++i) {
        co_await mutex->lock();
        // 立即释放，不做任何操作
        mutex->unlock();
    }
    g_test9_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试10：tryLock 在竞争环境下
// ============================================================================
std::atomic<int> g_test10_try_success{0};
std::atomic<int> g_test10_try_fail{0};
std::atomic<int> g_test10_completed{0};
constexpr int TEST10_COROUTINE_COUNT = 20;

Coroutine testTryLockContention(AsyncMutex* mutex) {
    for (int i = 0; i < 10; ++i) {
        if (mutex->tryLock()) {
            g_test10_try_success.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(1ms);
            mutex->unlock();
        } else {
            g_test10_try_fail.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::sleep_for(1ms);
    }
    g_test10_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试11：waiterCount 在多等待者场景（使用 yield 让出执行权）
// ============================================================================
std::atomic<int> g_test11_max_waiters{0};
std::atomic<int> g_test11_completed{0};
std::atomic<bool> g_test11_holder_done{false};
std::atomic<int> g_test11_waiters_ready{0};
constexpr int TEST11_WAITER_COUNT = 5;

Coroutine testWaiterCountHolder(AsyncMutex* mutex) {
    co_await mutex->lock();

    // 使用 yield 让出执行权，让等待者有机会入队
    for (int i = 0; i < 100; ++i) {
        co_yield true;  // 让出执行权
        int waiters = mutex->waiterCount();
        int max = g_test11_max_waiters.load(std::memory_order_relaxed);
        while (waiters > max && !g_test11_max_waiters.compare_exchange_weak(max, waiters));
        if (waiters >= TEST11_WAITER_COUNT) break;
    }

    mutex->unlock();
    g_test11_holder_done = true;
    co_return;
}

Coroutine testWaiterCountWaiter(AsyncMutex* mutex) {
    co_await mutex->lock();
    mutex->unlock();
    g_test11_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试12：队列容量边界（小容量队列）
// ============================================================================
std::atomic<int> g_test12_completed{0};
constexpr int TEST12_COROUTINE_COUNT = 20;  // 超过初始容量

Coroutine testQueueCapacity(AsyncMutex* mutex) {
    co_await mutex->lock();
    std::this_thread::sleep_for(1ms);
    mutex->unlock();
    g_test12_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试13：协程取消/提前退出场景模拟
// ============================================================================
std::atomic<int> g_test13_completed{0};

Coroutine testEarlyExit(AsyncMutex* mutex, bool shouldExit) {
    co_await mutex->lock();
    if (shouldExit) {
        mutex->unlock();
        co_return;  // 提前退出
    }
    std::this_thread::sleep_for(5ms);
    mutex->unlock();
    g_test13_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试14：混合 lock 和 tryLock
// ============================================================================
std::atomic<int> g_test14_lock_count{0};
std::atomic<int> g_test14_trylock_count{0};
std::atomic<int> g_test14_completed{0};
constexpr int TEST14_COROUTINE_COUNT = 10;

Coroutine testMixedLockTryLock(AsyncMutex* mutex, bool useTryLock) {
    for (int i = 0; i < 5; ++i) {
        if (useTryLock) {
            while (!mutex->tryLock()) {
                // 自旋等待
                std::this_thread::sleep_for(1ms);
            }
            g_test14_trylock_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            co_await mutex->lock();
            g_test14_lock_count.fetch_add(1, std::memory_order_relaxed);
        }
        mutex->unlock();
    }
    g_test14_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试15：长时间持有锁
// ============================================================================
std::atomic<int> g_test15_completed{0};
std::atomic<bool> g_test15_long_holder_done{false};

Coroutine testLongHold(AsyncMutex* mutex) {
    co_await mutex->lock();
    // 长时间持有
    std::this_thread::sleep_for(100ms);
    mutex->unlock();
    g_test15_long_holder_done = true;
    co_return;
}

Coroutine testLongHoldWaiter(AsyncMutex* mutex) {
    co_await mutex->lock();
    mutex->unlock();
    g_test15_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试16：scopedLock RAII 守卫
// ============================================================================
std::atomic<int> g_test16_counter{0};
std::atomic<int> g_test16_completed{0};
constexpr int TEST16_COROUTINE_COUNT = 10;

Coroutine testScopedLock(AsyncMutex* mutex) {
    for (int i = 0; i < 5; ++i) {
        auto guard = co_await mutex->scopedLock();
        g_test16_counter++;
        // guard 析构时自动释放锁
    }
    g_test16_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试17：scopedLock 提前释放
// ============================================================================
std::atomic<int> g_test17_counter{0};
std::atomic<bool> g_test17_done{false};

Coroutine testScopedLockEarlyUnlock(AsyncMutex* mutex) {
    {
        auto guard = co_await mutex->scopedLock();
        g_test17_counter++;
        guard.unlock();  // 提前释放
        // 此时锁已释放，但 guard 仍在作用域内
    }
    g_test17_done = true;
    co_return;
}

// ============================================================================
// 测试18：scopedLock 移动语义
// ============================================================================
std::atomic<bool> g_test18_done{false};

Coroutine testScopedLockMove(AsyncMutex* mutex) {
    auto guard1 = co_await mutex->scopedLock();

    // 移动守卫
    auto guard2 = std::move(guard1);

    // guard1 不再持有锁
    if (!guard1.ownsLock() && guard2.ownsLock()) {
        g_test18_done = true;
    }

    // guard2 析构时释放锁
    co_return;
}

// ============================================================================
// 测试19：跨调度器（多线程）场景
// ============================================================================
std::atomic<int> g_test19_counter{0};
std::atomic<int> g_test19_completed{0};
constexpr int TEST19_SCHEDULER_COUNT = 3;
constexpr int TEST19_OPS_PER_SCHEDULER = 20;

Coroutine testCrossScheduler(AsyncMutex* mutex) {
    for (int i = 0; i < TEST19_OPS_PER_SCHEDULER; ++i) {
        co_await mutex->lock();
        // 非原子操作，验证互斥性
        int val = g_test19_counter.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(1ms);  // 增加竞争窗口
        g_test19_counter.store(val + 1, std::memory_order_relaxed);
        mutex->unlock();
    }
    g_test19_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 测试20：跨调度器 scopedLock
// ============================================================================
std::atomic<int> g_test20_counter{0};
std::atomic<int> g_test20_completed{0};

Coroutine testCrossSchedulerScopedLock(AsyncMutex* mutex) {
    for (int i = 0; i < 10; ++i) {
        auto guard = co_await mutex->scopedLock();
        int val = g_test20_counter.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(1ms);
        g_test20_counter.store(val + 1, std::memory_order_relaxed);
    }
    g_test20_completed.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// ============================================================================
// 主函数
// ============================================================================
void runTests() {
    LogInfo("========================================");
    LogInfo("AsyncMutex Unit Tests");
    LogInfo("========================================");

#if defined(USE_EPOLL) || defined(USE_KQUEUE) || defined(USE_IOURING)

    // 测试1：基本 lock/unlock
    {
        LogInfo("\n--- Test 1: Basic lock/unlock ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);  // 小容量队列

        scheduler.start();
        scheduler.spawn(testBasicLockUnlock(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test1_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 2s) break;
        }

        scheduler.stop();

        if (g_test1_done) {
            LogInfo("[PASS] Basic lock/unlock completed");
            g_passed++;
        } else {
            LogError("[FAIL] Basic lock/unlock timeout");
        }
    }

    // 测试2：tryLock
    {
        LogInfo("\n--- Test 2: tryLock ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();
        scheduler.spawn(testTryLock(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test2_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 2s) break;
        }

        scheduler.stop();

        if (g_test2_done && g_test2_try_failed) {
            LogInfo("[PASS] tryLock failed when lock is held");
            g_passed++;
        } else {
            LogError("[FAIL] tryLock test: done={}, try_failed={}", g_test2_done.load(), g_test2_try_failed.load());
        }
    }

    // 测试3：多协程竞争（互斥性验证）
    {
        LogInfo("\n--- Test 3: Mutual exclusion ({} coroutines) ---", TEST3_COROUTINE_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(16);

        scheduler.start();

        for (int i = 0; i < TEST3_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testMutualExclusion(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test3_completed < TEST3_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 10s) break;
        }

        scheduler.stop();

        bool passed = (g_test3_completed == TEST3_COROUTINE_COUNT) &&
                      (g_test3_max_concurrent == 1) &&
                      (g_test3_counter == TEST3_COROUTINE_COUNT);

        if (passed) {
            LogInfo("[PASS] Mutual exclusion: completed={}, max_concurrent={}, counter={}",
                    g_test3_completed.load(), g_test3_max_concurrent.load(), g_test3_counter.load());
            g_passed++;
        } else {
            LogError("[FAIL] Mutual exclusion: completed={}/{}, max_concurrent={} (expected 1), counter={}",
                    g_test3_completed.load(), TEST3_COROUTINE_COUNT,
                    g_test3_max_concurrent.load(), g_test3_counter.load());
        }
    }

    // 测试4：公平性测试
    {
        LogInfo("\n--- Test 4: Fairness (FIFO order) ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();

        // 按顺序添加协程
        for (int i = 0; i < TEST4_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testFairness(&mutex, i));
            std::this_thread::sleep_for(10ms);  // 确保顺序
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test4_completed < TEST4_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 10s) break;
        }

        scheduler.stop();

        bool all_completed = (g_test4_completed == TEST4_COROUTINE_COUNT);
        bool first_is_zero = !g_test4_order.empty() && g_test4_order[0] == 0;

        std::string order_str;
        for (int id : g_test4_order) {
            order_str += std::to_string(id) + " ";
        }

        if (all_completed && first_is_zero) {
            LogInfo("[PASS] Fairness: order = {}", order_str);
            g_passed++;
        } else {
            LogError("[FAIL] Fairness: completed={}/{}, first_is_zero={}, order={}",
                    g_test4_completed.load(), TEST4_COROUTINE_COUNT, first_is_zero, order_str);
        }
    }

    // 测试5：压力测试
    {
        LogInfo("\n--- Test 5: Stress test ({} coroutines x {} increments) ---",
                TEST5_COROUTINE_COUNT, TEST5_INCREMENT_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(32);

        scheduler.start();

        for (int i = 0; i < TEST5_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testStress(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test5_completed < TEST5_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 60s) break;
        }

        scheduler.stop();

        int expected_sum = TEST5_COROUTINE_COUNT * TEST5_INCREMENT_COUNT;
        bool passed = (g_test5_completed == TEST5_COROUTINE_COUNT) &&
                      (g_test5_sum == expected_sum);

        if (passed) {
            LogInfo("[PASS] Stress test: completed={}, sum={} (expected {})",
                    g_test5_completed.load(), g_test5_sum.load(), expected_sum);
            g_passed++;
        } else {
            LogError("[FAIL] Stress test: completed={}/{}, sum={} (expected {})",
                    g_test5_completed.load(), TEST5_COROUTINE_COUNT,
                    g_test5_sum.load(), expected_sum);
        }
    }

    // 测试6：isLocked 状态检查
    {
        LogInfo("\n--- Test 6: isLocked state ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        bool initially_unlocked = !mutex.isLocked();

        scheduler.start();
        scheduler.spawn(testIsLocked(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test6_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 2s) break;
        }

        scheduler.stop();

        bool finally_unlocked = !mutex.isLocked();

        if (initially_unlocked && g_test6_locked_inside && finally_unlocked) {
            LogInfo("[PASS] isLocked: initial=unlocked, inside=locked, final=unlocked");
            g_passed++;
        } else {
            LogError("[FAIL] isLocked: initial={}, inside={}, final={}",
                    initially_unlocked ? "unlocked" : "locked",
                    g_test6_locked_inside ? "locked" : "unlocked",
                    finally_unlocked ? "unlocked" : "locked");
        }
    }

    // 测试7：waiterCount 检查（简化版）
    {
        LogInfo("\n--- Test 7: waiterCount ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();
        scheduler.spawn(testWaiterCountSingle(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test7_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 2s) break;
        }

        scheduler.stop();

        // 单协程时 waiterCount 应该为 0
        if (g_test7_done && g_test7_waiterCount == 0) {
            LogInfo("[PASS] waiterCount = {} (expected 0 for single coroutine)", g_test7_waiterCount.load());
            g_passed++;
        } else {
            LogError("[FAIL] waiterCount = {}, done = {}", g_test7_waiterCount.load(), g_test7_done.load());
        }
    }

    // 测试8：竞态条件测试（高并发）
    {
        LogInfo("\n--- Test 8: Race condition test ({} coroutines x {} increments) ---",
                TEST8_COROUTINE_COUNT, TEST8_INCREMENT_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(64);

        scheduler.start();

        for (int i = 0; i < TEST8_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testRaceCondition(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test8_completed < TEST8_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 60s) break;
        }

        scheduler.stop();

        int expected = TEST8_COROUTINE_COUNT * TEST8_INCREMENT_COUNT;
        bool passed = (g_test8_completed == TEST8_COROUTINE_COUNT) &&
                      (g_test8_race_counter == expected);

        if (passed) {
            LogInfo("[PASS] Race condition test: completed={}, counter={} (expected {})",
                    g_test8_completed.load(), g_test8_race_counter.load(), expected);
            g_passed++;
        } else {
            LogError("[FAIL] Race condition test: completed={}/{}, counter={} (expected {})",
                    g_test8_completed.load(), TEST8_COROUTINE_COUNT,
                    g_test8_race_counter.load(), expected);
        }
    }

    // 测试9：快速 lock/unlock 切换
    {
        LogInfo("\n--- Test 9: Rapid lock/unlock ({} iterations x 5 coroutines) ---", TEST9_ITERATIONS);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(16);

        scheduler.start();

        for (int i = 0; i < 5; ++i) {
            scheduler.spawn(testRapidLockUnlock(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test9_completed < 5) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        if (g_test9_completed == 5) {
            LogInfo("[PASS] Rapid lock/unlock: completed={}", g_test9_completed.load());
            g_passed++;
        } else {
            LogError("[FAIL] Rapid lock/unlock: completed={}/5", g_test9_completed.load());
        }
    }

    // 测试10：tryLock 竞争
    {
        LogInfo("\n--- Test 10: tryLock contention ({} coroutines) ---", TEST10_COROUTINE_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(32);

        scheduler.start();

        for (int i = 0; i < TEST10_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testTryLockContention(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test10_completed < TEST10_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        bool passed = (g_test10_completed == TEST10_COROUTINE_COUNT);
        int total_attempts = g_test10_try_success + g_test10_try_fail;

        if (passed) {
            LogInfo("[PASS] tryLock contention: completed={}, success={}, fail={}, total={}",
                    g_test10_completed.load(), g_test10_try_success.load(),
                    g_test10_try_fail.load(), total_attempts);
            g_passed++;
        } else {
            LogError("[FAIL] tryLock contention: completed={}/{}",
                    g_test10_completed.load(), TEST10_COROUTINE_COUNT);
        }
    }

    // 测试11：waiterCount 多等待者
    {
        LogInfo("\n--- Test 11: waiterCount with multiple waiters ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(16);

        scheduler.start();

        // 先启动持有者
        scheduler.spawn(testWaiterCountHolder(&mutex));

        // 再启动等待者
        for (int i = 0; i < TEST11_WAITER_COUNT; ++i) {
            scheduler.spawn(testWaiterCountWaiter(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (!g_test11_holder_done || g_test11_completed < TEST11_WAITER_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        bool passed = g_test11_holder_done &&
                      (g_test11_completed == TEST11_WAITER_COUNT) &&
                      (g_test11_max_waiters > 0);

        if (passed) {
            LogInfo("[PASS] waiterCount: max_waiters={}, completed={}",
                    g_test11_max_waiters.load(), g_test11_completed.load());
            g_passed++;
        } else {
            LogError("[FAIL] waiterCount: holder_done={}, max_waiters={}, completed={}/{}",
                    g_test11_holder_done.load(), g_test11_max_waiters.load(),
                    g_test11_completed.load(), TEST11_WAITER_COUNT);
        }
    }

    // 测试12：队列容量边界
    {
        LogInfo("\n--- Test 12: Queue capacity boundary ({} coroutines, capacity=4) ---",
                TEST12_COROUTINE_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(4);  // 小容量

        scheduler.start();

        for (int i = 0; i < TEST12_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testQueueCapacity(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test12_completed < TEST12_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        if (g_test12_completed == TEST12_COROUTINE_COUNT) {
            LogInfo("[PASS] Queue capacity: completed={}", g_test12_completed.load());
            g_passed++;
        } else {
            LogError("[FAIL] Queue capacity: completed={}/{}",
                    g_test12_completed.load(), TEST12_COROUTINE_COUNT);
        }
    }

    // 测试13：提前退出
    {
        LogInfo("\n--- Test 13: Early exit scenario ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();

        // 一些提前退出，一些正常完成
        for (int i = 0; i < 5; ++i) {
            scheduler.spawn(testEarlyExit(&mutex, i % 2 == 0));  // 偶数提前退出
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test13_completed < 2) {  // 只有2个会增加计数
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 10s) break;
        }

        scheduler.stop();

        // 验证锁状态正常
        bool lock_ok = !mutex.isLocked();

        if (g_test13_completed == 2 && lock_ok) {
            LogInfo("[PASS] Early exit: completed={}, lock_released={}",
                    g_test13_completed.load(), lock_ok);
            g_passed++;
        } else {
            LogError("[FAIL] Early exit: completed={} (expected 2), lock_released={}",
                    g_test13_completed.load(), lock_ok);
        }
    }

    // 测试14：混合 lock 和 tryLock
    {
        LogInfo("\n--- Test 14: Mixed lock and tryLock ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(16);

        scheduler.start();

        for (int i = 0; i < TEST14_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testMixedLockTryLock(&mutex, i % 2 == 0));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test14_completed < TEST14_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 60s) break;
        }

        scheduler.stop();

        int expected_total = TEST14_COROUTINE_COUNT * 5;
        int actual_total = g_test14_lock_count + g_test14_trylock_count;
        bool passed = (g_test14_completed == TEST14_COROUTINE_COUNT) &&
                      (actual_total == expected_total);

        if (passed) {
            LogInfo("[PASS] Mixed lock/tryLock: completed={}, lock={}, trylock={}, total={}",
                    g_test14_completed.load(), g_test14_lock_count.load(),
                    g_test14_trylock_count.load(), actual_total);
            g_passed++;
        } else {
            LogError("[FAIL] Mixed lock/tryLock: completed={}/{}, total={}/{}",
                    g_test14_completed.load(), TEST14_COROUTINE_COUNT,
                    actual_total, expected_total);
        }
    }

    // 测试15：长时间持有锁
    {
        LogInfo("\n--- Test 15: Long hold scenario ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();

        scheduler.spawn(testLongHold(&mutex));
        std::this_thread::sleep_for(10ms);

        // 添加等待者
        for (int i = 0; i < 3; ++i) {
            scheduler.spawn(testLongHoldWaiter(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (!g_test15_long_holder_done || g_test15_completed < 3) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        bool passed = g_test15_long_holder_done && (g_test15_completed == 3);

        if (passed) {
            LogInfo("[PASS] Long hold: holder_done={}, waiters_completed={}",
                    g_test15_long_holder_done.load(), g_test15_completed.load());
            g_passed++;
        } else {
            LogError("[FAIL] Long hold: holder_done={}, waiters_completed={}/3",
                    g_test15_long_holder_done.load(), g_test15_completed.load());
        }
    }

    // 测试16：scopedLock RAII 守卫
    {
        LogInfo("\n--- Test 16: scopedLock RAII guard ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(16);

        scheduler.start();

        for (int i = 0; i < TEST16_COROUTINE_COUNT; ++i) {
            scheduler.spawn(testScopedLock(&mutex));
        }

        auto start = std::chrono::steady_clock::now();
        while (g_test16_completed < TEST16_COROUTINE_COUNT) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        int expected = TEST16_COROUTINE_COUNT * 5;
        bool passed = (g_test16_completed == TEST16_COROUTINE_COUNT) &&
                      (g_test16_counter == expected);

        if (passed) {
            LogInfo("[PASS] scopedLock: completed={}, counter={} (expected {})",
                    g_test16_completed.load(), g_test16_counter.load(), expected);
            g_passed++;
        } else {
            LogError("[FAIL] scopedLock: completed={}/{}, counter={}/{}",
                    g_test16_completed.load(), TEST16_COROUTINE_COUNT,
                    g_test16_counter.load(), expected);
        }
    }

    // 测试17：scopedLock 提前释放
    {
        LogInfo("\n--- Test 17: scopedLock early unlock ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();
        scheduler.spawn(testScopedLockEarlyUnlock(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test17_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        bool lock_released = !mutex.isLocked();

        if (g_test17_done && lock_released) {
            LogInfo("[PASS] scopedLock early unlock: done={}, lock_released={}",
                    g_test17_done.load(), lock_released);
            g_passed++;
        } else {
            LogError("[FAIL] scopedLock early unlock: done={}, lock_released={}",
                    g_test17_done.load(), lock_released);
        }
    }

    // 测试18：scopedLock 移动语义
    {
        LogInfo("\n--- Test 18: scopedLock move semantics ---");
        g_total++;

        IOSchedulerType scheduler;
        AsyncMutex mutex(8);

        scheduler.start();
        scheduler.spawn(testScopedLockMove(&mutex));

        auto start = std::chrono::steady_clock::now();
        while (!g_test18_done) {
            std::this_thread::sleep_for(10ms);
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        bool lock_released = !mutex.isLocked();

        if (g_test18_done && lock_released) {
            LogInfo("[PASS] scopedLock move: done={}, lock_released={}",
                    g_test18_done.load(), lock_released);
            g_passed++;
        } else {
            LogError("[FAIL] scopedLock move: done={}, lock_released={}",
                    g_test18_done.load(), lock_released);
        }
    }

    // 测试19：跨调度器（多线程）场景
    {
        LogInfo("\n--- Test 19: Cross-scheduler ({} schedulers x {} ops) ---",
                TEST19_SCHEDULER_COUNT, TEST19_OPS_PER_SCHEDULER);
        g_total++;

        AsyncMutex mutex(32);
        std::vector<std::unique_ptr<IOSchedulerType>> schedulers;
        std::vector<std::thread> threads;

        // 创建多个调度器，每个在独立线程运行
        for (int i = 0; i < TEST19_SCHEDULER_COUNT; ++i) {
            schedulers.push_back(std::make_unique<IOSchedulerType>());
        }

        // 启动调度器并提交协程
        for (int i = 0; i < TEST19_SCHEDULER_COUNT; ++i) {
            threads.emplace_back([&schedulers, &mutex, i]() {
                schedulers[i]->start();
                schedulers[i]->spawn(testCrossScheduler(&mutex));

                // 等待协程完成
                auto start = std::chrono::steady_clock::now();
                while (g_test19_completed <= i) {
                    std::this_thread::sleep_for(10ms);
                    if (std::chrono::steady_clock::now() - start > 60s) break;
                }

                schedulers[i]->stop();
            });
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }

        int expected = TEST19_SCHEDULER_COUNT * TEST19_OPS_PER_SCHEDULER;
        bool passed = (g_test19_completed == TEST19_SCHEDULER_COUNT) &&
                      (g_test19_counter == expected);

        if (passed) {
            LogInfo("[PASS] Cross-scheduler: completed={}, counter={} (expected {})",
                    g_test19_completed.load(), g_test19_counter.load(), expected);
            g_passed++;
        } else {
            LogError("[FAIL] Cross-scheduler: completed={}/{}, counter={} (expected {})",
                    g_test19_completed.load(), TEST19_SCHEDULER_COUNT,
                    g_test19_counter.load(), expected);
        }
    }

    // 测试20：跨调度器 scopedLock
    {
        LogInfo("\n--- Test 20: Cross-scheduler scopedLock ---");
        g_total++;

        AsyncMutex mutex(32);
        std::vector<std::unique_ptr<IOSchedulerType>> schedulers;
        std::vector<std::thread> threads;

        for (int i = 0; i < 3; ++i) {
            schedulers.push_back(std::make_unique<IOSchedulerType>());
        }

        for (int i = 0; i < 3; ++i) {
            threads.emplace_back([&schedulers, &mutex, i]() {
                schedulers[i]->start();
                schedulers[i]->spawn(testCrossSchedulerScopedLock(&mutex));

                auto start = std::chrono::steady_clock::now();
                while (g_test20_completed <= i) {
                    std::this_thread::sleep_for(10ms);
                    if (std::chrono::steady_clock::now() - start > 60s) break;
                }

                schedulers[i]->stop();
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        int expected = 3 * 10;  // 3 schedulers x 10 ops
        bool passed = (g_test20_completed == 3) && (g_test20_counter == expected);

        if (passed) {
            LogInfo("[PASS] Cross-scheduler scopedLock: completed={}, counter={}",
                    g_test20_completed.load(), g_test20_counter.load());
            g_passed++;
        } else {
            LogError("[FAIL] Cross-scheduler scopedLock: completed={}/3, counter={}/{}",
                    g_test20_completed.load(), g_test20_counter.load(), expected);
        }
    }

#else
    LogWarn("No IO scheduler available, skipping tests");
#endif

    // 打印测试结果
    LogInfo("\n========================================");
    LogInfo("Test Results: {}/{} passed", g_passed.load(), g_total.load());
    LogInfo("========================================");
}

int main() {
    runTests();
    return (g_passed == g_total) ? 0 : 1;
}
