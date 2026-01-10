/**
 * @file test_udp_server.cc
 * @brief UDP Echo Server 测试
 */

#include <atomic>
#include <cstring>
#include "galay-kernel/async/UdpSocket.h"
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

// UDP Echo服务器协程
Coroutine udpEchoServer() {
    g_total++;
    LogInfo("UDP Server starting...");
    UdpSocket socket;
    LogDebug("Socket created, fd={}", socket.handle().fd);

    // 设置选项
    auto optResult = socket.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    optResult = socket.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 8080);
    auto bindResult = socket.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }
    LogDebug("Bind successful");

    LogInfo("UDP Server listening on 127.0.0.1:8080");
    g_server_ready = true;

    // Echo循环 - 接收并回显3个数据报
    char buffer[65536];
    int message_count = 0;
    for (int i = 0; i < 3; ++i) {
        Host from;
        LogDebug("Waiting for recvfrom...");
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
        if (!recvResult) {
            LogError("Recvfrom error: {}", recvResult.error().message());
            g_failed++;
            break;
        }

        auto& bytes = recvResult.value();
        LogInfo("Received from {}:{}: {}", from.ip(), from.port(), bytes.toStringView());

        // Echo回发送方
        auto sendResult = co_await socket.sendto(bytes.c_str(), bytes.size(), from);
        if (!sendResult) {
            LogError("Sendto error: {}", sendResult.error().message());
            g_failed++;
            break;
        }
        LogDebug("Sent {} bytes back to {}:{}", sendResult.value(), from.ip(), from.port());
        message_count++;
    }

    if (message_count == 3) {
        LogInfo("Test PASSED: Received and echoed 3 messages");
        g_passed++;
    } else {
        LogError("Test FAILED: Only received {} messages", message_count);
        g_failed++;
    }

    co_await socket.close();
    LogInfo("UDP Server stopped");
    g_test_done = true;
    co_return;
}

int main() {
    LogInfo("========================================");
    LogInfo("UDP Echo Server Test");
    LogInfo("========================================\n");

    galay::test::TestResultWriter writer("test_udp_server");

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    IOSchedulerType scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(udpEchoServer());
    LogDebug("Server coroutine spawned");

    // 等待服务器准备就绪
    while (!g_server_ready.load() && !g_test_done.load()) {
        // 使用调度器的空闲等待
    }

    if (!g_test_done.load()) {
        LogInfo("Server is ready, waiting for client datagrams...");
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
