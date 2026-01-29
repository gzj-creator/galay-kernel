/**
 * @file bench_mpsc_channel.cc
 * @brief MpscChannel 性能压测与正确性验证
 *
 * 测试项目：
 * 1. 吞吐量：单生产者/多生产者场景下的消息吞吐量
 * 2. 延迟：消息从发送到接收的延迟
 * 3. 正确性：验证消息不丢失、不重复
 * 4. 跨调度器：多调度器场景下的性能和正确性
 */

#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>
#include <set>
#include <mutex>
#include <numeric>
#include "galay-kernel/concurrency/MpscChannel.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/common/Log.h"

using namespace galay::kernel;
using namespace std::chrono_literals;

// ============== 压测参数 ==============
constexpr int WARMUP_COUNT = 10000;
constexpr int THROUGHPUT_MESSAGES = 1000000;
constexpr int LATENCY_MESSAGES = 100000;
constexpr int CORRECTNESS_MESSAGES = 100000;

// ============== 全局计数器 ==============
std::atomic<int64_t> g_sent{0};
std::atomic<int64_t> g_received{0};
std::atomic<int64_t> g_sum{0};
std::atomic<int64_t> g_latency_sum_ns{0};
std::atomic<int64_t> g_latency_count{0};
std::atomic<bool> g_consumer_done{false};
std::atomic<bool> g_producer_done{false};

// 正确性验证
std::mutex g_received_mutex;
std::set<int64_t> g_received_set;

void resetCounters() {
    g_sent = 0;
    g_received = 0;
    g_sum = 0;
    g_latency_sum_ns = 0;
    g_latency_count = 0;
    g_consumer_done = false;
    g_producer_done = false;
    g_received_set.clear();
}

// ============== 消息结构 ==============
struct TimestampedMessage {
    int64_t id;
    std::chrono::steady_clock::time_point send_time;
};

// ============== 消费者协程 ==============

// 简单消费者（吞吐量测试）
Coroutine simpleConsumer(MpscChannel<int64_t>* channel, int64_t expected_count) {
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

// 批量消费者
Coroutine batchConsumer(MpscChannel<int64_t>* channel, int64_t expected_count) {
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

// 延迟测试消费者
Coroutine latencyConsumer(MpscChannel<TimestampedMessage>* channel, int64_t expected_count) {
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

// 正确性验证消费者
Coroutine correctnessConsumer(MpscChannel<int64_t>* channel, int64_t expected_count) {
    while (g_received < expected_count) {
        auto value = co_await channel->recv();
        if (value) {
            {
                std::lock_guard<std::mutex> lock(g_received_mutex);
                g_received_set.insert(*value);
            }
            g_received.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_consumer_done = true;
    co_return;
}

// ============== 生产者函数 ==============

// 单线程生产者
void singleProducer(MpscChannel<int64_t>* channel, int64_t count) {
    
    for (int64_t i = 0; i < count; ++i) {
        channel->send(i);
    }
    g_sent.store(count, std::memory_order_relaxed);
    g_producer_done = true;
}

// 多线程生产者
void multiProducer(MpscChannel<int64_t>* channel, int64_t start, int64_t count) {
    
    for (int64_t i = 0; i < count; ++i) {
        channel->send(start + i);
    }
    g_sent.fetch_add(count, std::memory_order_relaxed);
}

// 延迟测试生产者
void latencyProducer(MpscChannel<TimestampedMessage>* channel, int64_t count) {

    for (int64_t i = 0; i < count; ++i) {
        TimestampedMessage msg;
        msg.id = i;
        msg.send_time = std::chrono::steady_clock::now();
        channel->send(std::move(msg));
    }
    g_sent.store(count, std::memory_order_relaxed);
    g_producer_done = true;
}

// ============== 压测函数 ==============

// 1. 单生产者吞吐量测试
void benchSingleProducerThroughput(int64_t message_count) {
    LogInfo("--- Single Producer Throughput Test ({} messages) ---", message_count);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();

    // 正式测试（移除预热，避免状态不同步）
    scheduler.spawn(simpleConsumer(&channel, message_count));

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        singleProducer(&channel, message_count);
    });

    // 等待完成
    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    producer.join();
    scheduler.stop();

    // 验证
    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 2. 多生产者吞吐量测试
void benchMultiProducerThroughput(int producer_count, int64_t total_messages) {
    LogInfo("--- Multi Producer Throughput Test ({} producers, {} messages) ---",
            producer_count, total_messages);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();
    scheduler.spawn(simpleConsumer(&channel, total_messages));

    int64_t per_producer = total_messages / producer_count;

    auto start = std::chrono::steady_clock::now();

    // 启动多个生产者线程
    std::vector<std::thread> producers;
    for (int i = 0; i < producer_count; ++i) {
        int64_t start_id = i * per_producer;
        producers.emplace_back(multiProducer, &channel, start_id, per_producer);
    }

    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }

    // 等待消费者完成
    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)total_messages / ms * 1000.0;

    scheduler.stop();

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
}

// 3. 批量接收吞吐量测试
void benchBatchReceiveThroughput(int64_t message_count) {
    LogInfo("--- Batch Receive Throughput Test ({} messages) ---", message_count);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();
    scheduler.spawn(batchConsumer(&channel, message_count));

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        singleProducer(&channel, message_count);
    });

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    producer.join();
    scheduler.stop();

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 4. 延迟测试
void benchLatency(int64_t message_count) {
    LogInfo("--- Latency Test ({} messages) ---", message_count);
    resetCounters();

    MpscChannel<TimestampedMessage> channel;
    ComputeScheduler scheduler;

    scheduler.start();
    scheduler.spawn(latencyConsumer(&channel, message_count));

    std::thread producer([&]() {
        latencyProducer(&channel, message_count);
    });

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    producer.join();
    scheduler.stop();

    double avg_latency_us = (double)g_latency_sum_ns / g_latency_count / 1000.0;

    LogInfo("  messages={}, avg_latency={:.2f}us", g_received.load(), avg_latency_us);
}

// 5. 正确性验证测试
void benchCorrectness(int producer_count, int64_t total_messages) {
    LogInfo("--- Correctness Test ({} producers, {} messages) ---",
            producer_count, total_messages);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    scheduler.start();
    scheduler.spawn(correctnessConsumer(&channel, total_messages));

    int64_t per_producer = total_messages / producer_count;

    // 启动多个生产者线程
    std::vector<std::thread> producers;
    for (int i = 0; i < producer_count; ++i) {
        int64_t start_id = i * per_producer;
        producers.emplace_back(multiProducer, &channel, start_id, per_producer);
    }

    for (auto& t : producers) {
        t.join();
    }

    while (!g_consumer_done) {
        std::this_thread::sleep_for(1ms);
    }

    scheduler.stop();

    // 验证正确性
    bool no_loss = (g_received_set.size() == (size_t)total_messages);
    bool no_duplicate = (g_received == total_messages);

    // 检查是否所有消息都收到
    std::set<int64_t> expected_set;
    for (int i = 0; i < producer_count; ++i) {
        int64_t start_id = i * per_producer;
        for (int64_t j = 0; j < per_producer; ++j) {
            expected_set.insert(start_id + j);
        }
    }

    bool all_received = (g_received_set == expected_set);

    LogInfo("  sent={}, received={}, unique={}",
            g_sent.load(), g_received.load(), g_received_set.size());
    LogInfo("  no_loss={}, no_duplicate={}, all_correct={}",
            no_loss ? "YES" : "NO",
            no_duplicate ? "YES" : "NO",
            all_received ? "YES" : "NO");

    if (!all_received) {
        LogError("  CORRECTNESS FAILED!");
    }
}

// 6. 跨调度器测试
void benchCrossScheduler(int64_t message_count) {
    LogInfo("--- Cross-Scheduler Test ({} messages) ---", message_count);
    resetCounters();

    MpscChannel<int64_t> channel;

    // 消费者调度器
    ComputeScheduler consumerScheduler;

    // 生产者协程
    auto producerCoro = [](MpscChannel<int64_t>* ch, int64_t count) -> Coroutine {

        for (int64_t i = 0; i < count; ++i) {
            ch->send(i);
            g_sent.fetch_add(1, std::memory_order_relaxed);
            if (i % 100 == 0) {
                co_yield true;  // 让出执行权
            }
        }
        g_producer_done = true;
        co_return;
    };

    auto start = std::chrono::steady_clock::now();

    // 启动消费者线程
    std::thread consumerThread([&]() {
        consumerScheduler.start();
        consumerScheduler.spawn(simpleConsumer(&channel, message_count));

        while (!g_consumer_done) {
            std::this_thread::sleep_for(1ms);
        }
        consumerScheduler.stop();
    });

    // 启动生产者线程（独立调度器）
    std::thread producerThread([&]() {
        ComputeScheduler producerScheduler;
        producerScheduler.start();
        producerScheduler.spawn(producerCoro(&channel, message_count));

        while (!g_producer_done) {
            std::this_thread::sleep_for(1ms);
        }
        producerScheduler.stop();
    });

    producerThread.join();
    consumerThread.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double throughput = (double)message_count / ms * 1000.0;

    int64_t expected_sum = (message_count - 1) * message_count / 2;
    bool correct = (g_received == message_count) && (g_sum == expected_sum);

    LogInfo("  sent={}, received={}, time={}ms, throughput={:.0f} msg/s",
            g_sent.load(), g_received.load(), ms, throughput);
    LogInfo("  sum={} (expected {}), correct={}",
            g_sum.load(), expected_sum, correct ? "YES" : "NO");
}

// 7. 持续压力测试
void benchSustained(int duration_sec) {
    LogInfo("--- Sustained Load Test ({}s) ---", duration_sec);
    resetCounters();

    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;

    std::atomic<bool> running{true};

    // 消费者协程
    auto sustainedConsumer = [](MpscChannel<int64_t>* ch, std::atomic<bool>* run) -> Coroutine {
        while (*run || ch->size() > 0) {
            auto value = co_await ch->recv();
            if (value) {
                g_received.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_consumer_done = true;
        co_return;
    };

    scheduler.start();
    scheduler.spawn(sustainedConsumer(&channel, &running));

    auto start = std::chrono::steady_clock::now();
    auto end_time = start + std::chrono::seconds(duration_sec);

    // 生产者线程
    std::vector<std::thread> producers(4);
    for(auto& producer: producers) {
        producer = std::thread([&]() {
            int64_t id = 0;
            while (running) {
                channel.send(id++);
                g_sent.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 监控
    int64_t last_received = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(1s);
        int64_t current = g_received.load();
        int64_t delta = current - last_received;
        LogInfo("  throughput: {}/s, total sent: {}, received: {}",
                delta, g_sent.load(), current);
        last_received = current;
    }

    running = false;
    for(auto& producer: producers) {
        producer.join();
    }

    // 等待消费者处理完剩余消息
    while (!g_consumer_done && channel.size() > 0) {
        std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    scheduler.stop();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    double avg_throughput = (double)g_received / ms * 1000.0;

    LogInfo("  total: sent={}, received={}, avg throughput: {:.0f}/s",
            g_sent.load(), g_received.load(), avg_throughput);
}

int main(int argc, char* argv[]) {
    LogInfo("=== MpscChannel Benchmark ===");
    LogInfo("");

    // 1. 单生产者吞吐量
    benchSingleProducerThroughput(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 2. 多生产者吞吐量
    benchMultiProducerThroughput(4, THROUGHPUT_MESSAGES);
    LogInfo("");

    // 3. 批量接收吞吐量
    benchBatchReceiveThroughput(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 4. 延迟测试
    benchLatency(LATENCY_MESSAGES);
    LogInfo("");

    // 5. 正确性验证
    benchCorrectness(4, CORRECTNESS_MESSAGES);
    LogInfo("");

    // 6. 跨调度器测试
    benchCrossScheduler(THROUGHPUT_MESSAGES);
    LogInfo("");

    // 7. 持续压力测试
    benchSustained(5);
    LogInfo("");

    LogInfo("=== Benchmark Complete ===");

    return 0;
}
