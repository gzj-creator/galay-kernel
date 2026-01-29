/**
 * @file test_unsafe_channel.cc
 * @brief UnsafeChannel 单元测试
 */

#include <atomic>
#include <chrono>
#include <vector>
#include "galay-kernel/concurrency/UnsafeChannel.h"
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

// ============================================================================
// 测试1：基本 send/recv
// ============================================================================
std::atomic<bool> g_test1_done{false};
int g_test1_received{0};

Coroutine testBasicSendRecv(UnsafeChannel<int>* channel) {
    auto value = co_await channel->recv();
    if (value && *value == 42) {
        g_test1_received = *value;
    }
    g_test1_done = true;
    co_return;
}

Coroutine testBasicSender(UnsafeChannel<int>* channel) {
    channel->send(42);
    co_return;
}

// ============================================================================
// 测试2：多次 send/recv
// ============================================================================
int g_test2_sum{0};
std::atomic<bool> g_test2_done{false};
constexpr int TEST2_COUNT = 10;

Coroutine testMultipleRecv(UnsafeChannel<int>* channel) {
    for (int i = 0; i < TEST2_COUNT; ++i) {
        auto value = co_await channel->recv();
        if (value) {
            g_test2_sum += *value;
        }
    }
    g_test2_done = true;
    co_return;
}

Coroutine testMultipleSend(UnsafeChannel<int>* channel) {
    for (int i = 0; i < TEST2_COUNT; ++i) {
        channel->send(i + 1);
        co_yield true;
    }
    co_return;
}

// ============================================================================
// 测试3：批量发送/接收
// ============================================================================
int g_test3_total{0};
std::atomic<bool> g_test3_done{false};

Coroutine testBatchRecv(UnsafeChannel<int>* channel) {
    auto batch = co_await channel->recvBatch(100);
    if (batch) {
        for (int v : *batch) {
            g_test3_total += v;
        }
    }
    g_test3_done = true;
    co_return;
}

Coroutine testBatchSend(UnsafeChannel<int>* channel) {
    std::vector<int> data = {1, 2, 3, 4, 5};
    channel->sendBatch(data);
    co_return;
}

// ============================================================================
// 测试4：try_recv（非阻塞）
// ============================================================================
std::atomic<bool> g_test4_done{false};
int g_test4_value{0};

Coroutine testTryRecv(UnsafeChannel<int>* channel) {
    // 先尝试接收（应该为空）
    auto empty = channel->tryRecv();
    if (empty) {
        g_test4_done = true;
        co_return;  // 不应该到这里
    }

    // 等待数据
    auto value = co_await channel->recv();
    if (value) {
        g_test4_value = *value;
    }
    g_test4_done = true;
    co_return;
}

Coroutine testTryRecvSender(UnsafeChannel<int>* channel) {
    co_yield true;  // 让消费者先执行
    channel->send(99);
    co_return;
}

// ============================================================================
// 测试5：同调度器多生产者单消费者
// ============================================================================
int g_test5_sum{0};
int g_test5_recv_count{0};
std::atomic<bool> g_test5_done{false};
constexpr int TEST5_PRODUCER_COUNT = 5;
constexpr int TEST5_MSG_PER_PRODUCER = 10;

Coroutine testMultiProducerConsumer(UnsafeChannel<int>* channel) {
    int expected = TEST5_PRODUCER_COUNT * TEST5_MSG_PER_PRODUCER;
    while (g_test5_recv_count < expected) {
        auto value = co_await channel->recv();
        if (value) {
            g_test5_sum += *value;
            g_test5_recv_count++;
        }
    }
    g_test5_done = true;
    co_return;
}

Coroutine testProducer(UnsafeChannel<int>* channel, int id) {
    for (int i = 0; i < TEST5_MSG_PER_PRODUCER; ++i) {
        channel->send(id * 100 + i);
        co_yield true;
    }
    co_return;
}

// ============================================================================
// 测试6：空通道等待
// ============================================================================
std::atomic<bool> g_test6_waiting{false};
std::atomic<bool> g_test6_received{false};
std::atomic<bool> g_test6_done{false};

Coroutine testEmptyChannelWait(UnsafeChannel<int>* channel) {
    g_test6_waiting = true;
    auto value = co_await channel->recv();
    g_test6_received = value.has_value();
    g_test6_done = true;
    co_return;
}

Coroutine testDelayedSend(UnsafeChannel<int>* channel) {
    // 等待消费者开始等待
    while (!g_test6_waiting) {
        co_yield true;
    }
    channel->send(123);
    co_return;
}

// ============================================================================
// 测试7：size() 和 empty()
// ============================================================================
std::atomic<bool> g_test7_done{false};

Coroutine testSizeAndEmpty(UnsafeChannel<int>* channel) {
    // 消费所有数据
    while (!channel->empty()) {
        co_await channel->recv();
    }
    g_test7_done = true;
    co_return;
}

// ============================================================================
// 测试8：批量发送多次
// ============================================================================
int g_test8_total{0};
int g_test8_count{0};
std::atomic<bool> g_test8_done{false};

Coroutine testBatchRecvMultiple(UnsafeChannel<int>* channel) {
    // 接收所有数据
    for (int i = 0; i < 3; ++i) {
        auto batch = co_await channel->recvBatch(100);
        if (batch) {
            g_test8_count += batch->size();
            for (int v : *batch) {
                g_test8_total += v;
            }
        }
    }
    g_test8_done = true;
    co_return;
}

Coroutine testBatchSendMultiple(UnsafeChannel<int>* channel) {
    std::vector<int> batch1 = {1, 2, 3};
    std::vector<int> batch2 = {4, 5, 6, 7};
    std::vector<int> batch3 = {8, 9, 10};

    channel->sendBatch(batch1);
    co_yield true;
    channel->sendBatch(batch2);
    co_yield true;
    channel->sendBatch(batch3);
    co_return;
}

// ============================================================================
// 测试9：字符串类型
// ============================================================================
std::atomic<bool> g_test9_done{false};
std::string g_test9_result;

Coroutine testStringRecv(UnsafeChannel<std::string>* channel) {
    auto value = co_await channel->recv();
    if (value) {
        g_test9_result = *value;
    }
    g_test9_done = true;
    co_return;
}

Coroutine testStringSend(UnsafeChannel<std::string>* channel) {
    channel->send(std::string("Hello, UnsafeChannel!"));
    co_return;
}

// ============================================================================
// 测试10：高并发（同调度器内）
// ============================================================================
int g_test10_received{0};
std::atomic<bool> g_test10_done{false};
constexpr int TEST10_TOTAL = 1000;

Coroutine testHighConcurrencyConsumer(UnsafeChannel<int>* channel) {
    while (g_test10_received < TEST10_TOTAL) {
        auto value = co_await channel->recv();
        if (value) {
            g_test10_received++;
        }
    }
    g_test10_done = true;
    co_return;
}

Coroutine testHighConcurrencyProducer(UnsafeChannel<int>* channel, int start, int count) {
    for (int i = 0; i < count; ++i) {
        channel->send(start + i);
        if (i % 10 == 0) {
            co_yield true;
        }
    }
    co_return;
}

// ============================================================================
// 主函数
// ============================================================================
void runTests() {
    LogInfo("========================================");
    LogInfo("UnsafeChannel Unit Tests");
    LogInfo("========================================");

#if defined(USE_EPOLL) || defined(USE_KQUEUE) || defined(USE_IOURING)

    // 测试1：基本 send/recv
    {
        LogInfo("\n--- Test 1: Basic send/recv ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testBasicSendRecv(&channel));
        scheduler.spawn(testBasicSender(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test1_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        if (g_test1_done && g_test1_received == 42) {
            LogInfo("[PASS] Basic send/recv: received={}", g_test1_received);
            g_passed++;
        } else {
            LogError("[FAIL] Basic send/recv: done={}, received={}",
                    g_test1_done.load(), g_test1_received);
        }
    }

    // 测试2：多次 send/recv
    {
        LogInfo("\n--- Test 2: Multiple send/recv ({} messages) ---", TEST2_COUNT);
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testMultipleRecv(&channel));
        scheduler.spawn(testMultipleSend(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test2_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 10s) break;
        }

        scheduler.stop();

        int expected_sum = TEST2_COUNT * (TEST2_COUNT + 1) / 2;  // 1+2+...+10 = 55
        if (g_test2_done && g_test2_sum == expected_sum) {
            LogInfo("[PASS] Multiple send/recv: sum={} (expected {})",
                    g_test2_sum, expected_sum);
            g_passed++;
        } else {
            LogError("[FAIL] Multiple send/recv: done={}, sum={} (expected {})",
                    g_test2_done.load(), g_test2_sum, expected_sum);
        }
    }

    // 测试3：批量发送/接收
    {
        LogInfo("\n--- Test 3: Batch send/recv ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testBatchRecv(&channel));
        scheduler.spawn(testBatchSend(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test3_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        int expected = 15;  // 1+2+3+4+5
        if (g_test3_done && g_test3_total == expected) {
            LogInfo("[PASS] Batch send/recv: total={} (expected {})",
                    g_test3_total, expected);
            g_passed++;
        } else {
            LogError("[FAIL] Batch send/recv: done={}, total={} (expected {})",
                    g_test3_done.load(), g_test3_total, expected);
        }
    }

    // 测试4：try_recv
    {
        LogInfo("\n--- Test 4: try_recv (non-blocking) ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testTryRecv(&channel));
        scheduler.spawn(testTryRecvSender(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test4_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        if (g_test4_done && g_test4_value == 99) {
            LogInfo("[PASS] try_recv: value={}", g_test4_value);
            g_passed++;
        } else {
            LogError("[FAIL] try_recv: done={}, value={}",
                    g_test4_done.load(), g_test4_value);
        }
    }

    // 测试5：同调度器多生产者
    {
        LogInfo("\n--- Test 5: Same-scheduler multi-producer ({} producers x {} messages) ---",
                TEST5_PRODUCER_COUNT, TEST5_MSG_PER_PRODUCER);
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testMultiProducerConsumer(&channel));

        // 启动多个生产者协程（同一调度器内）
        for (int i = 0; i < TEST5_PRODUCER_COUNT; ++i) {
            scheduler.spawn(testProducer(&channel, i));
        }

        auto start = std::chrono::steady_clock::now();
        while (!g_test5_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 10s) break;
        }

        scheduler.stop();

        int expected_count = TEST5_PRODUCER_COUNT * TEST5_MSG_PER_PRODUCER;
        if (g_test5_done && g_test5_recv_count == expected_count) {
            LogInfo("[PASS] Same-scheduler multi-producer: received={}, sum={}",
                    g_test5_recv_count, g_test5_sum);
            g_passed++;
        } else {
            LogError("[FAIL] Same-scheduler multi-producer: done={}, received={}/{}",
                    g_test5_done.load(), g_test5_recv_count, expected_count);
        }
    }

    // 测试6：空通道等待
    {
        LogInfo("\n--- Test 6: Empty channel wait ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testEmptyChannelWait(&channel));
        scheduler.spawn(testDelayedSend(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test6_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        if (g_test6_done && g_test6_received) {
            LogInfo("[PASS] Empty channel wait: received after wait");
            g_passed++;
        } else {
            LogError("[FAIL] Empty channel wait: done={}, received={}",
                    g_test6_done.load(), g_test6_received.load());
        }
    }

    // 测试7：size() 和 empty()
    {
        LogInfo("\n--- Test 7: size() and empty() ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        // 发送数据
        for (int i = 0; i < 5; ++i) {
            channel.send(i);
        }

        bool size_ok = (channel.size() == 5);
        bool not_empty = !channel.empty();

        scheduler.start();
        scheduler.spawn(testSizeAndEmpty(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test7_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        bool empty_after = channel.empty();

        if (size_ok && not_empty && empty_after && g_test7_done) {
            LogInfo("[PASS] size/empty: initial_size=5, empty_after=true");
            g_passed++;
        } else {
            LogError("[FAIL] size/empty: size_ok={}, not_empty={}, empty_after={}",
                    size_ok, not_empty, empty_after);
        }
    }

    // 测试8：批量发送多次
    {
        LogInfo("\n--- Test 8: Batch send multiple ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testBatchRecvMultiple(&channel));
        scheduler.spawn(testBatchSendMultiple(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test8_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        int expected_total = 55;  // 1+2+...+10
        int expected_count = 10;

        if (g_test8_done && g_test8_count == expected_count && g_test8_total == expected_total) {
            LogInfo("[PASS] Batch send multiple: count={}, total={}",
                    g_test8_count, g_test8_total);
            g_passed++;
        } else {
            LogError("[FAIL] Batch send multiple: done={}, count={}/{}, total={}/{}",
                    g_test8_done.load(), g_test8_count, expected_count,
                    g_test8_total, expected_total);
        }
    }

    // 测试9：字符串类型
    {
        LogInfo("\n--- Test 9: String channel ---");
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<std::string> channel;

        scheduler.start();
        scheduler.spawn(testStringRecv(&channel));
        scheduler.spawn(testStringSend(&channel));

        auto start = std::chrono::steady_clock::now();
        while (!g_test9_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }

        scheduler.stop();

        if (g_test9_done && g_test9_result == "Hello, UnsafeChannel!") {
            LogInfo("[PASS] String channel: result=\"{}\"", g_test9_result);
            g_passed++;
        } else {
            LogError("[FAIL] String channel: done={}, result=\"{}\"",
                    g_test9_done.load(), g_test9_result);
        }
    }

    // 测试10：高并发（同调度器内）
    {
        LogInfo("\n--- Test 10: High concurrency ({} messages) ---", TEST10_TOTAL);
        g_total++;

        IOSchedulerType scheduler;
        UnsafeChannel<int> channel;

        scheduler.start();
        scheduler.spawn(testHighConcurrencyConsumer(&channel));

        // 启动多个生产者协程
        int per_producer = TEST10_TOTAL / 4;
        for (int i = 0; i < 4; ++i) {
            scheduler.spawn(testHighConcurrencyProducer(&channel, i * per_producer, per_producer));
        }

        auto start = std::chrono::steady_clock::now();
        while (!g_test10_done) {
            // 使用调度器的空闲等待
            if (std::chrono::steady_clock::now() - start > 30s) break;
        }

        scheduler.stop();

        if (g_test10_done && g_test10_received == TEST10_TOTAL) {
            LogInfo("[PASS] High concurrency: received={}",
                    g_test10_received);
            g_passed++;
        } else {
            LogError("[FAIL] High concurrency: done={}, received={}/{}",
                    g_test10_done.load(), g_test10_received, TEST10_TOTAL);
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
    galay::test::TestResultWriter resultWriter("test_unsafe_channel");
    runTests();

    // 写入测试结果
    resultWriter.addTest();
    if (g_passed == g_total) {
        resultWriter.addPassed();
    } else {
        resultWriter.addFailed();
    }
    resultWriter.writeResult();

    return (g_passed == g_total) ? 0 : 1;
}
