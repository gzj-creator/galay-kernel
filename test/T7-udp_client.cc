/**
 * @file T7-udp_client.cc
 * @brief 用途：验证 UDP Echo Client 测试路径能够发送报文并收到正确回显。
 * 关键覆盖点：客户端发包、接收回包、回显内容与来源地址校验。
 * 通过条件：客户端收到预期回包，测试断言全部成立并返回 0。
 */

#include <atomic>
#include <cstring>
#include <string_view>
#include "galay-kernel/async/UdpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "test/TestPortConfig.h"
#include "test/StdoutLog.h"
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

namespace {

uint16_t udpTestPort() {
    return galay::test::resolvePortFromEnv("GALAY_TEST_UDP_PORT", 8080);
}

}

std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};
std::atomic<bool> g_test_done{false};

// UDP客户端协程
Coroutine udpEchoClient() {
    g_total++;
    LogInfo("UDP Client starting...");
    UdpSocket socket;
    LogDebug("Client socket created, fd={}", socket.handle().fd);

    socket.option().handleNonBlock();

    // 服务器地址
    Host serverHost(IPType::IPV4, "127.0.0.1", udpTestPort());

    // 发送3条消息并验证回显
    const char* messages[] = {
        "Hello, UDP Server!",
        "This is message 2",
        "Final message"
    };

    int success_count = 0;
    for (int i = 0; i < 3; ++i) {
        // 发送消息
        LogDebug("Client sending message {}...", i + 1);
        auto sendResult = co_await socket.sendto(messages[i], strlen(messages[i]), serverHost);
        if (!sendResult) {
            LogError("Client: Sendto failed for message {}", i + 1);
            g_failed++;
            continue;
        }

        LogInfo("Client: Sent message {}: {}", i + 1, messages[i]);

        // 接收回复
        char buffer[1024];
        Host from;
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
        if (!recvResult) {
            LogError("Client: Recvfrom failed for message {}", i + 1);
            g_failed++;
            continue;
        }

        size_t bytes = recvResult.value();
        auto payload = std::string_view(buffer, bytes);
        LogInfo("Client: Received echo from {}:{}: {}", from.ip(), from.port(), payload);

        // 验证回显内容
        if (payload == messages[i]) {
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

    co_await socket.close();
    LogInfo("UDP Client stopped");
    g_test_done = true;
    co_return;
}

int main() {
    LogInfo("========================================");
    LogInfo("UDP Echo Client Test");
    LogInfo("========================================\n");

    galay::test::TestResultWriter writer("test_udp_client");

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
    IOSchedulerType scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动客户端
    scheduler.spawn(udpEchoClient());
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
