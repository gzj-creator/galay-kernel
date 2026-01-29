/**
 * @file test_tcp_server.cc
 * @brief TCP Echo Server 测试
 */

#include <iostream>
#include <atomic>
#include <cstring>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"
#include "test_result_writer.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
using IOSchedulerType = galay::kernel::KqueueScheduler;
#endif

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
using IOSchedulerType = galay::kernel::EpollScheduler;
#endif

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
using IOSchedulerType = galay::kernel::IOUringScheduler;
#endif

using namespace galay::async;
using namespace galay::kernel;

std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};
std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_test_done{false};

// Echo服务器协程
Coroutine echoServer() {
    g_total++;
    LogInfo("TCP Server starting...");
    TcpSocket listener;

    // 设置选项
    auto optResult = listener.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    optResult = listener.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 8080);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }
    LogDebug("Bind successful");

    // 监听
    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    LogInfo("TCP Server listening on 127.0.0.1:8080");
    g_server_ready = true;

    // 接受连接
    Host clientHost;
    LogDebug("Waiting for accept...");
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("Failed to accept: {}", acceptResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    LogInfo("Client connected from {}:{}", clientHost.ip(), clientHost.port());

    // 创建客户端socket
    TcpSocket client(acceptResult.value());
    client.option().handleNonBlock();

    // Echo循环 - 接收3条消息
    char buffer[1024];
    int message_count = 0;
    while (message_count < 3) {
        LogDebug("Waiting for recv...");
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult) {
            LogError("Recv error: {}", recvResult.error().message());
            g_failed++;
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            LogInfo("Client disconnected");
            break;
        }

        LogInfo("Received: {}", bytes.toStringView());

        auto sendResult = co_await client.send(bytes.c_str(), bytes.size());
        if (!sendResult) {
            LogError("Send error: {}", sendResult.error().message());
            g_failed++;
            break;
        }
        LogDebug("Sent {} bytes", sendResult.value());
        message_count++;
    }

    if (message_count == 3) {
        LogInfo("Test PASSED: Received and echoed 3 messages");
        g_passed++;
    } else {
        LogError("Test FAILED: Only received {} messages", message_count);
        g_failed++;
    }

    co_await client.close();
    co_await listener.close();
    LogInfo("TCP Server stopped");
    g_test_done = true;
    co_return;
}

int main() {
    LogInfo("========================================");
    LogInfo("TCP Echo Server Test");
    LogInfo("========================================\n");

    galay::test::TestResultWriter writer("test_tcp_server");

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    IOSchedulerType scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(echoServer());
    LogDebug("Server coroutine spawned");

    // 等待服务器准备就绪
    while (!g_server_ready.load() && !g_test_done.load()) {
        // 使用调度器的空闲等待，而不是 sleep
    }

    if (!g_test_done.load()) {
        LogInfo("Server is ready, waiting for client connection...");
    }

    // 等待测试完成
    while (!g_test_done.load()) {
        // 使用调度器的空闲等待
    }

    scheduler.stop();
    LogInfo("Test finished");
#else
    LogWarn("This test requires kqueue (macOS), epoll or io_uring (Linux)");
    g_failed++;
#endif

    // 写入测试结果
    writer.addTest();
    if (g_passed > 0) {
        writer.addPassed();
    }
    if (g_failed > 0) {
        writer.addFailed();
    }
    writer.writeResult();

    LogInfo("========================================");
    LogInfo("Test Results: Total={}, Passed={}, Failed={}", g_total.load(), g_passed.load(), g_failed.load());
    LogInfo("========================================");

    return g_failed > 0 ? 1 : 0;
}
