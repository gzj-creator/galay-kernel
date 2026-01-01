#include <iostream>
#include <cstring>
#include <atomic>
#include <chrono>
#include <vector>
#include "galay-kernel/async/UdpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
#endif

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
#endif

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
#endif

using namespace galay::async;
using namespace galay::kernel;

// 全局统计
std::atomic<uint64_t> g_total_sent{0};
std::atomic<uint64_t> g_total_received{0};
std::atomic<uint64_t> g_total_bytes_sent{0};
std::atomic<uint64_t> g_total_bytes_received{0};
std::atomic<bool> g_running{true};

// 配置参数
constexpr int NUM_CLIENTS = 100;        // 并发客户端数量
constexpr int MESSAGES_PER_CLIENT = 100; // 每个客户端发送的消息数
constexpr int MESSAGE_SIZE = 1024;       // 消息大小（字节）
constexpr int TEST_DURATION_SEC = 10;    // 测试持续时间（秒）

// UDP Echo服务器协程 - 高性能版本
Coroutine udpBenchmarkServer(IOScheduler* scheduler) {
    LogInfo("UDP Benchmark Server starting...");
    UdpSocket socket(scheduler);

    auto createResult = socket.create(IPType::IPV4);
    if (!createResult) {
        LogError("Failed to create socket: {}", createResult.error().message());
        co_return;
    }

    socket.option().handleReuseAddr();
    socket.option().handleNonBlock();

    // 设置接收缓冲区大小
    int recv_buf_size = 8 * 1024 * 1024; // 8MB
    setsockopt(socket.handle().fd, SOL_SOCKET, SO_RCVBUF,
               &recv_buf_size, sizeof(recv_buf_size));

    Host bindHost(IPType::IPV4, "127.0.0.1", 9090);
    auto bindResult = socket.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    LogInfo("UDP Benchmark Server listening on 127.0.0.1:9090");

    char buffer[65536];
    while (g_running.load(std::memory_order_relaxed)) {
        Host from;
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);

        if (!recvResult) {
            if (recvResult.error().code() == EAGAIN ||
                recvResult.error().code() == EWOULDBLOCK) {
                continue;
            }
            LogError("Recvfrom error: {}", recvResult.error().message());
            break;
        }

        auto& bytes = recvResult.value();
        g_total_received.fetch_add(1, std::memory_order_relaxed);
        g_total_bytes_received.fetch_add(bytes.size(), std::memory_order_relaxed);

        // Echo回发送方
        auto sendResult = co_await socket.sendto(bytes.c_str(), bytes.size(), from);
        if (sendResult) {
            g_total_sent.fetch_add(1, std::memory_order_relaxed);
            g_total_bytes_sent.fetch_add(sendResult.value(), std::memory_order_relaxed);
        }
    }

    co_await socket.close();
    LogInfo("UDP Benchmark Server stopped");
    co_return;
}

// UDP客户端协程 - 压测版本
Coroutine udpBenchmarkClient(IOScheduler* scheduler, int client_id) {
    UdpSocket socket(scheduler);

    auto createResult = socket.create(IPType::IPV4);
    if (!createResult) {
        LogError("Client {}: Failed to create socket", client_id);
        co_return;
    }

    socket.option().handleNonBlock();

    // 设置发送缓冲区大小
    int send_buf_size = 2 * 1024 * 1024; // 2MB
    setsockopt(socket.handle().fd, SOL_SOCKET, SO_SNDBUF,
               &send_buf_size, sizeof(send_buf_size));

    Host serverHost(IPType::IPV4, "127.0.0.1", 9090);

    // 准备测试数据
    std::vector<char> message(MESSAGE_SIZE);
    snprintf(message.data(), MESSAGE_SIZE, "Client-%d-Message", client_id);

    char recv_buffer[65536];
    uint64_t local_sent = 0;
    uint64_t local_received = 0;

    // 发送消息
    for (int i = 0; i < MESSAGES_PER_CLIENT && g_running.load(std::memory_order_relaxed); ++i) {
        // 发送
        auto sendResult = co_await socket.sendto(message.data(), MESSAGE_SIZE, serverHost);
        if (sendResult) {
            local_sent++;
        }

        // 接收回显
        Host from;
        auto recvResult = co_await socket.recvfrom(recv_buffer, sizeof(recv_buffer), &from);
        if (recvResult) {
            local_received++;
        }
    }

    g_total_sent.fetch_add(local_sent, std::memory_order_relaxed);
    g_total_received.fetch_add(local_received, std::memory_order_relaxed);
    g_total_bytes_sent.fetch_add(local_sent * MESSAGE_SIZE, std::memory_order_relaxed);
    g_total_bytes_received.fetch_add(local_received * MESSAGE_SIZE, std::memory_order_relaxed);

    co_await socket.close();
    co_return;
}

// 统计输出协程
Coroutine statsReporter(IOScheduler* scheduler) {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_sent = 0;
    uint64_t last_received = 0;
    uint64_t last_bytes_sent = 0;
    uint64_t last_bytes_received = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        co_await std::suspend_always{};
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        uint64_t current_sent = g_total_sent.load(std::memory_order_relaxed);
        uint64_t current_received = g_total_received.load(std::memory_order_relaxed);
        uint64_t current_bytes_sent = g_total_bytes_sent.load(std::memory_order_relaxed);
        uint64_t current_bytes_received = g_total_bytes_received.load(std::memory_order_relaxed);

        uint64_t sent_per_sec = current_sent - last_sent;
        uint64_t received_per_sec = current_received - last_received;
        uint64_t bytes_sent_per_sec = current_bytes_sent - last_bytes_sent;
        uint64_t bytes_received_per_sec = current_bytes_received - last_bytes_received;

        LogInfo("[{}s] Sent: {} pkt/s ({:.2f} MB/s), Recv: {} pkt/s ({:.2f} MB/s), Total: {} sent, {} recv",
                elapsed,
                sent_per_sec,
                bytes_sent_per_sec / 1024.0 / 1024.0,
                received_per_sec,
                bytes_received_per_sec / 1024.0 / 1024.0,
                current_sent,
                current_received);

        last_sent = current_sent;
        last_received = current_received;
        last_bytes_sent = current_bytes_sent;
        last_bytes_received = current_bytes_received;
    }

    co_return;
}

void printBenchmarkResults(std::chrono::steady_clock::time_point start_time) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    double duration_sec = duration / 1000.0;

    uint64_t total_sent = g_total_sent.load();
    uint64_t total_received = g_total_received.load();
    uint64_t total_bytes_sent = g_total_bytes_sent.load();
    uint64_t total_bytes_received = g_total_bytes_received.load();

    LogInfo("\n========== UDP Benchmark Results ==========");
    LogInfo("Test Duration: {:.2f} seconds", duration_sec);
    LogInfo("Concurrent Clients: {}", NUM_CLIENTS);
    LogInfo("Messages per Client: {}", MESSAGES_PER_CLIENT);
    LogInfo("Message Size: {} bytes", MESSAGE_SIZE);
    LogInfo("");
    LogInfo("Total Packets Sent: {}", total_sent);
    LogInfo("Total Packets Received: {}", total_received);
    LogInfo("Packet Loss Rate: {:.2f}%",
            total_sent > 0 ? (1.0 - (double)total_received / total_sent) * 100.0 : 0.0);
    LogInfo("");
    LogInfo("Total Data Sent: {:.2f} MB", total_bytes_sent / 1024.0 / 1024.0);
    LogInfo("Total Data Received: {:.2f} MB", total_bytes_received / 1024.0 / 1024.0);
    LogInfo("");
    LogInfo("Average Throughput:");
    LogInfo("  Sent: {:.2f} pkt/s ({:.2f} MB/s)",
            total_sent / duration_sec,
            total_bytes_sent / duration_sec / 1024.0 / 1024.0);
    LogInfo("  Received: {:.2f} pkt/s ({:.2f} MB/s)",
            total_received / duration_sec,
            total_bytes_received / duration_sec / 1024.0 / 1024.0);
    LogInfo("==========================================\n");
}

int main() {
    LogInfo("UDP Socket Benchmark Test");
    LogInfo("Configuration: {} clients, {} messages/client, {} bytes/message",
            NUM_CLIENTS, MESSAGES_PER_CLIENT, MESSAGE_SIZE);

#ifdef USE_KQUEUE
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler (Linux)");
    EpollScheduler scheduler;
#elif defined(USE_IOURING)
    LogInfo("Using IOUringScheduler (Linux io_uring)");
    IOUringScheduler scheduler;
#else
    LogError("This benchmark requires kqueue (macOS), epoll or io_uring (Linux)");
    return 1;
#endif

    scheduler.start();
    LogInfo("Scheduler started");

    auto start_time = std::chrono::steady_clock::now();

    // 启动服务器
    scheduler.spawn(udpBenchmarkServer(&scheduler));
    LogInfo("Server started");

    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 启动统计报告协程
    // scheduler.spawn(statsReporter(&scheduler));

    // 启动多个客户端
    LogInfo("Starting {} clients...", NUM_CLIENTS);
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        scheduler.spawn(udpBenchmarkClient(&scheduler, i));
    }

    // 运行测试
    LogInfo("Benchmark running for {} seconds...", TEST_DURATION_SEC);
    std::this_thread::sleep_for(std::chrono::seconds(TEST_DURATION_SEC));

    // 停止测试
    g_running.store(false, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    scheduler.stop();
    LogInfo("Scheduler stopped");

    // 打印结果
    printBenchmarkResults(start_time);

    return 0;
}
