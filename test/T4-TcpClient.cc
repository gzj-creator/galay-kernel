/**
 * @file test_tcp_client.cc
 * @brief TCP Echo Client 测试
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
std::atomic<bool> g_test_done{false};

// 客户端协程
Coroutine echoClient() {
    g_total++;
    LogInfo("TCP Client starting...");
    TcpSocket client;
    LogDebug("Client socket created, fd={}", client.handle().fd);

    client.option().handleNonBlock();

    // 连接服务器
    Host serverHost(IPType::IPV4, "127.0.0.1", 8080);
    LogDebug("Client connecting to server...");
    auto connectResult = co_await client.connect(serverHost);
    if (!connectResult) {
        LogError("Client: Failed to connect: {}", connectResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    LogInfo("Client: Connected to server");

    // 发送3条消息并验证回显
    const char* messages[] = {
        "Hello, Server!",
        "This is message 2",
        "Final message"
    };

    int success_count = 0;
    for (int i = 0; i < 3; ++i) {
        // 发送消息
        auto sendResult = co_await client.send(messages[i], strlen(messages[i]));
        if (!sendResult) {
            LogError("Client: Send failed for message {}", i + 1);
            g_failed++;
            continue;
        }

        LogInfo("Client: Sent message {}: {}", i + 1, messages[i]);

        // 接收回复
        char buffer[1024];
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult) {
            LogError("Client: Recv failed for message {}", i + 1);
            g_failed++;
            continue;
        }

        auto& bytes = recvResult.value();
        LogInfo("Client: Received echo: {}", bytes.toStringView());

        // 验证回显内容
        if (bytes.toStringView() == messages[i]) {
            LogInfo("Client: Message {} echo verified", i + 1);
            success_count++;
        } else {
            LogError("Client: Message {} echo mismatch", i + 1);
            g_failed++;
        }
    }

    if (success_count == 3) {
        LogInfo("Test PASSED: All 3 messages echoed correctly");
        g_passed++;
    } else {
        LogError("Test FAILED: Only {} messages echoed correctly", success_count);
        if (g_failed == 0) g_failed++;
    }

    co_await client.close();
    LogInfo("TCP Client stopped");
    g_test_done = true;
    co_return;
}

int main() {
    LogInfo("========================================");
    LogInfo("TCP Echo Client Test");
    LogInfo("========================================\n");

    galay::test::TestResultWriter writer("test_tcp_client");

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    IOSchedulerType scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动客户端
    scheduler.spawn(echoClient());
    LogDebug("Client coroutine spawned");

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
