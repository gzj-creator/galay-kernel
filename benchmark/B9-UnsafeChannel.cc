/**
 * @file bench_unsafe_channel.cc
 * @brief UnsafeChannel 性能压测
 *
 * 测试项目：
 * 1. 吞吐量：同调度器内的消息吞吐量
 * 2. 延迟：消息从发送到接收的延迟
 * 3. 与 MpscChannel 的性能对比
 */

#include <atomic>
#include <chrono>
#include <vector>
#include "galay-kernel/concurrency/UnsafeChannel.h"
#include "galay-kernel/concurrency/MpscChannel.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/common/Log.h"

using namespace galay::kernel;
using namespace std::chrono_literals;

// ============== 压测参数 ==============
constexpr int64_t THROUGHPUT_MESSAGES = 1000000;
constexpr int64_t LATENCY_MESSAGES = 100000;

// ============== 全局计数器 ==============
std::atomic<int64_t> g_sent{0};
std::atomic<int64_t> g_received{0};
std::atomic<int64_t> g_sum{0};
std::atomic<int64_t> g_latency_sum_ns{0};
std::atomic<int64_t> g_latency_count{0};
std::atomic<bool> g_consumer_done{false};
std::atomic<bool> g_producer_done{false};

void resetCounters() {
    g_sent = 0;
    g_received = 0;
    g_sum = 0;
    g_latency_sum_ns = 0;
    g_latency_count = 0;
    g_consumer_done = false;
    g_producer_done = false;
}

// ============== 消息结构 ==============
struct TimestampedMessage {
    int64_t id;
    std::chrono::steady_clock::time_point send_time;
};

// ============== UnsafeChannel 消费者协程 ==============

Coroutine unsafeSimpleConsumer(UnsafeChannel<int64_t>* channel, int64_t expected_count) {
    int64_t received = 0;
    int64_t sum = 0;
    while (received < expected_count) {
        auto value = co_await channel->recv();
        if (value) {
            ++received;
            sum += *value;
        }
    }
    g_received.store(received, std::memory_order_relaxed);
    g_sum.store(sum, std::memory_order_relaxed);
    g_consumer_done = true;
    co_return;
}

Coroutine unsafeBatchConsumer(UnsafeChannel<int64_t>* channel, int64_t expected_count) {
    int64_t received = 0;
    int64_t sum = 0;
    while (received < expected_count) {
        auto batch = co_await channel->recvBatch(256);
        if (batch) {
            for (int64_t v : *batch) {
                sum += v;
            }
            received += batch->size();
        }
    }
    g_received.store(received, std::memory_order_relaxed);
    g_sum.store(sum, std::memory_order_relaxed);
    g_consumer_done = true;
    co_return;
}

Coroutine unsafeBatchedConsumer(UnsafeChannel<int64_t>* channel, int64_t expected_count, int64_t batch_limit) {
    int64_t received = 0;
    int64_t sum = 0;
    while (received < expected_count) {
        // 使用 recvBatched 攒批接收，带超时
        auto batch = co_await channel->recvBatched(batch_limit).timeout(10ms);
        if (batch) {
            for (int64_t v : *batch) {
                sum += v;
            }
            received += batch->size();
        }
    }
    g_received.store(received, std::memory_order_relaxed);
    g_sum.store(sum, std::memory_order_relaxed);
    g_consumer_done = true;
    co_return;
}

Coroutine unsafeLatencyConsumer(UnsafeChannel<TimestampedMessage>* channel, int64_t expected_count) {
    int64_t received = 0;
    int64_t latency_sum_ns = 0;
    while (received < expected_count) {
        auto msg = co_await channel->recv();
        if (msg) {
            auto now = std::chrono::steady_clock::now();
            auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - msg->send_time).count();
            latency_sum_ns += latency_ns;
            ++received;
        }
    }
    g_received.store(received, std::memory_order_relaxed);
    g_latency_sum_ns.store(latency_sum_ns, std::memory_order_relaxed);
    g_latency_count.store(received, std::memory_order_relaxed);
    g_consumer_done = true;
    co_return;
}

// ============== UnsafeChannel 生产者协程 ==============

Coroutine unsafeSimpleProducer(UnsafeChannel<int64_t>* channel, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        channel->send(i);
        if (i % 1000 == 0) {
            co_yield true;  // 让出执行权
        }
    }
    g_sent.store(count, std::memory_order_relaxed);
    g_producer_done = true;
    co_return;
}

Coroutine unsafeLatencyProducer(UnsafeChannel<TimestampedMessage>* channel, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        TimestampedMessage msg;
        msg.id = i;
        msg.send_time = std::chrono::steady_clock::now();
        channel->send(std::move(msg));
        if (i % 100 == 0) {
            co_yield true;
        }
    }
    g_sent.store(count, std::memory_order_relaxed);
    g_producer_done = true;
    co_return;
}

// ============== MpscChannel 消费者协程（用于对比）==============

Coroutine mpscSimpleConsumer(MpscChannel<int64_t>* channel, int64_t expected_count) {
    int64_t received = 0;
    int64_t sum = 0;
    while (received < expected_count) {
        auto value = co_await channel->recv();
        if (value) {
            ++received;
            sum += *value;
        }
    }
    g_received.store(received, std::memory_order_relaxed);
    g_sum.store(sum, std::memory_order_relaxed);
    g_consumer_done = true;
    co_return;
}

// ============== MpscChannel 生产者协程（用于对比）==============

Coroutine mpscSimpleProducer(MpscChannel<int64_t>* channel, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        channel->send(i);
        if (i % 1000 == 0) {
            co_yield true;
        }
    }
    g_sent.store(count, std::memory_order_relaxed);
    g_producer_done = true;
    co_return;
}

// ============== 压测函数 ==============

// 1. UnsafeChannel 单生产者吞吐量测试
void benchUnsafeChannelThroughput(int64_t message_count) {
    LogInfo("--- UnsafeChannel Throughput Test ({} messages) ---", message_count);
    resetCounters();

    UnsafeChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    auto start = std::chrono::steady_clock::now();

    scheduler.spawn(unsafeSimpleConsumer(&channel, message_count));
    scheduler.spawn(unsafeSimpleProducer(&channel, message_count));

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    scheduler.stop();

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 2. UnsafeChannel 批量接收吞吐量测试
void benchUnsafeChannelBatchThroughput(int64_t message_count) {
    LogInfo("--- UnsafeChannel Batch Receive Throughput Test ({} messages) ---", message_count);
    resetCounters();

    UnsafeChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    auto start = std::chrono::steady_clock::now();

    scheduler.spawn(unsafeBatchConsumer(&channel, message_count));
    scheduler.spawn(unsafeSimpleProducer(&channel, message_count));

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    scheduler.stop();

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 2b. UnsafeChannel recvBatched 攒批接收吞吐量测试
void benchUnsafeChannelBatchedThroughput(int64_t message_count, int64_t batch_limit) {
    LogInfo("--- UnsafeChannel recvBatched Throughput Test ({} messages, limit={}) ---",
            message_count, batch_limit);
    resetCounters();

    UnsafeChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    auto start = std::chrono::steady_clock::now();

    scheduler.spawn(unsafeBatchedConsumer(&channel, message_count, batch_limit));
    scheduler.spawn(unsafeSimpleProducer(&channel, message_count));

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    scheduler.stop();

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 3. UnsafeChannel 延迟测试
void benchUnsafeChannelLatency(int64_t message_count) {
    LogInfo("--- UnsafeChannel Latency Test ({} messages) ---", message_count);
    resetCounters();

    UnsafeChannel<TimestampedMessage> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    scheduler.spawn(unsafeLatencyConsumer(&channel, message_count));
    scheduler.spawn(unsafeLatencyProducer(&channel, message_count));

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    scheduler.stop();

    double avg_latency_us = (double)g_latency_sum_ns / g_latency_count / 1000.0;

    LogInfo("  messages={}, avg_latency={:.2f}us", g_received.load(), avg_latency_us);
}

// 4. MpscChannel 吞吐量测试（同调度器，用于对比）
void benchMpscChannelThroughput(int64_t message_count) {
    LogInfo("--- MpscChannel Throughput Test (same scheduler, {} messages) ---", message_count);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    auto start = std::chrono::steady_clock::now();

    scheduler.spawn(mpscSimpleConsumer(&channel, message_count));
    scheduler.spawn(mpscSimpleProducer(&channel, message_count));

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    scheduler.stop();

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 5. 性能对比总结
void benchComparison(int64_t message_count) {
    LogInfo("\n=== Performance Comparison ({} messages) ===", message_count);

    // UnsafeChannel 测试
    resetCounters();
    UnsafeChannel<int64_t> unsafeChannel;
    ComputeScheduler scheduler1;

    scheduler1.start();
    auto start1 = std::chrono::steady_clock::now();
    scheduler1.spawn(unsafeSimpleConsumer(&unsafeChannel, message_count));
    scheduler1.spawn(unsafeSimpleProducer(&unsafeChannel, message_count));
    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }
    auto elapsed1 = std::chrono::steady_clock::now() - start1;
    auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed1).count();
    double throughput1 = (double)message_count / ms1 * 1000.0;
    scheduler1.stop();

    // MpscChannel 测试
    resetCounters();
    MpscChannel<int64_t> mpscChannel;
    ComputeScheduler scheduler2;

    scheduler2.start();
    auto start2 = std::chrono::steady_clock::now();
    scheduler2.spawn(mpscSimpleConsumer(&mpscChannel, message_count));
    scheduler2.spawn(mpscSimpleProducer(&mpscChannel, message_count));
    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }
    auto elapsed2 = std::chrono::steady_clock::now() - start2;
    auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed2).count();
    double throughput2 = (double)message_count / ms2 * 1000.0;
    scheduler2.stop();

    // 输出对比结果
    LogInfo("");
    LogInfo("| Channel Type   | Time (ms) | Throughput (msg/s) |");
    LogInfo("|----------------|-----------|-------------------|");
    LogInfo("| UnsafeChannel  | {:>9} | {:>17.0f} |", ms1, throughput1);
    LogInfo("| MpscChannel    | {:>9} | {:>17.0f} |", ms2, throughput2);
    LogInfo("");

    double speedup = throughput1 / throughput2;
    LogInfo("UnsafeChannel is {:.2f}x {} than MpscChannel (same scheduler)",
            speedup > 1 ? speedup : 1/speedup,
            speedup > 1 ? "faster" : "slower");
}

int main(int argc, char* argv[]) {
    LogInfo("=== UnsafeChannel Benchmark ===");
    LogInfo("");

    // 1. UnsafeChannel 吞吐量
    benchUnsafeChannelThroughput(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 2. UnsafeChannel 批量接收吞吐量
    benchUnsafeChannelBatchThroughput(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 2b. UnsafeChannel recvBatched 攒批接收吞吐量（不同 limit）
    benchUnsafeChannelBatchedThroughput(THROUGHPUT_MESSAGES, 100);
    LogInfo("");
    benchUnsafeChannelBatchedThroughput(THROUGHPUT_MESSAGES, 500);
    LogInfo("");
    benchUnsafeChannelBatchedThroughput(THROUGHPUT_MESSAGES, 1000);
    LogInfo("");

    // 3. UnsafeChannel 延迟测试
    benchUnsafeChannelLatency(LATENCY_MESSAGES);
    LogInfo("");

    // 4. MpscChannel 吞吐量（对比）
    benchMpscChannelThroughput(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 5. 性能对比
    benchComparison(THROUGHPUT_MESSAGES);
    LogInfo("");

    LogInfo("=== Benchmark Complete ===");

    return 0;
}
