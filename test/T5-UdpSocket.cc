#include <exception>
#include <iostream>
#include <cstring>
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

// UDP Echo服务器协程
Coroutine udpEchoServer() {
    LogInfo("UDP Server starting...");
    UdpSocket socket;
    LogDebug("Socket created, fd={}", socket.handle().fd);

    // 设置选项
    auto optResult = socket.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        co_return;
    }

    optResult = socket.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 8080);
    auto bindResult = socket.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }
    LogDebug("Bind successful");

    LogInfo("UDP Server listening on 127.0.0.1:8080");

    // Echo循环 - 接收并回显3个数据报
    char buffer[65536];
    for (int i = 0; i < 3; ++i) {
        Host from;
        LogDebug("Waiting for recvfrom...");
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
        if (!recvResult) {
            LogError("Recvfrom error: {}", recvResult.error().message());
            break;
        }

        auto& bytes = recvResult.value();
        LogInfo("Received from {}:{}: {}", from.ip(), from.port(), bytes.toStringView());

        // Echo回发送方
        auto sendResult = co_await socket.sendto(bytes.c_str(), bytes.size(), from);
        if (!sendResult) {
            LogError("Sendto error: {}", sendResult.error().message());
            break;
        }
        LogDebug("Sent {} bytes back to {}:{}", sendResult.value(), from.ip(), from.port());
    }

    co_await socket.close();
    LogInfo("UDP Server stopped");
    co_return;
}

// UDP客户端协程
Coroutine udpEchoClient() {
    LogInfo("UDP Client starting...");
    UdpSocket socket;
    LogDebug("Client socket created, fd={}", socket.handle().fd);

    socket.option().handleNonBlock();

    // 服务器地址
    Host serverHost(IPType::IPV4, "127.0.0.1", 8080);

    // 发送3条消息
    const char* messages[] = {
        "Hello, UDP Server!",
        "This is message 2",
        "Final message"
    };

    for (int i = 0; i < 3; ++i) {
        // 发送消息
        LogDebug("Client sending message {}...", i + 1);
        auto sendResult = co_await socket.sendto(messages[i], strlen(messages[i]), serverHost);
        if (!sendResult) {
            LogError("Client: Sendto failed");
            continue;
        }

        LogInfo("Client: Sent message {}: {}", i + 1, messages[i]);

        // 接收回复
        char buffer[1024];
        Host from;
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
        if (!recvResult) {
            LogError("Client: Recvfrom failed");
            continue;
        }

        auto& bytes = recvResult.value();
        LogInfo("Client: Received echo from {}:{}: {}", from.ip(), from.port(), bytes.toStringView());
    }

    co_await socket.close();
    LogInfo("UDP Client stopped");
    co_return;
}

int main() {
    LogInfo("UdpSocket Test");

#ifdef USE_KQUEUE
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(udpEchoServer());
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(udpEchoClient());
    LogDebug("Client coroutine spawned");

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    scheduler.stop();
    LogInfo("Test finished");
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler (Linux)");
    EpollScheduler scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(udpEchoServer());
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(udpEchoClient());
    LogDebug("Client coroutine spawned");

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    scheduler.stop();
    LogInfo("Test finished");
#elif defined(USE_IOURING)
    LogInfo("Using IOUringScheduler (Linux io_uring)");
    IOUringScheduler scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(udpEchoServer());
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(udpEchoClient());
    LogDebug("Client coroutine spawned");

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    scheduler.stop();
    LogInfo("Test finished");
#else
    LogWarn("This test requires kqueue (macOS), epoll or io_uring (Linux)");
#endif

    return 0;
}
