#include <iostream>
#include <cstring>
#include "galay-kernel/async/TcpSocket.h"
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

// Echo服务器协程
Coroutine echoServer(IOScheduler* scheduler) {
    LogInfo("Server starting...");
    TcpSocket listener(scheduler);

    // 创建socket
    auto createResult = listener.create(IPType::IPV4);
    if (!createResult) {
        LogError("Failed to create socket: {}", createResult.error().message());
        co_return;
    }
    LogDebug("Socket created, fd={}", listener.handle().fd);

    // 设置选项
    auto optResult = listener.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        co_return;
    }

    optResult = listener.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 8080);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }
    LogDebug("Bindsuccessful");

    // 监听
    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("Server listening on 127.0.0.1:8080");

    // 接受连接
    Host clientHost;
    LogDebug("Waiting for accept...");
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("Failed to accept: {}", acceptResult.error().message());
        co_return;
    }

    LogInfo("Client connected from {}:{}", clientHost.ip(), clientHost.port());

    // 创建客户端socket
    TcpSocket client(scheduler, acceptResult.value());
    client.option().handleNonBlock();

    // Echo循环
    char buffer[1024];
    while (true) {
        LogDebug("Waiting for recv...");
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult) {
            LogError("Recv error: {}", recvResult.error().message());
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
            break;
        }
        LogDebug("Sent {} bytes", sendResult.value());
    }

    co_await client.close();
    co_await listener.close();
    LogInfo("Server stopped");
    co_return;
}

// 客户端协程
Coroutine echoClient(IOScheduler* scheduler) {
    LogInfo("Client starting...");
    TcpSocket client(scheduler);

    // 创建socket
    auto createResult = client.create(IPType::IPV4);
    if (!createResult) {
        LogError("Client: Failed to create socket");
        co_return;
    }
    LogDebug("Client socket created, fd={}", client.handle().fd);

    client.option().handleNonBlock();

    // 连接服务器
    Host serverHost(IPType::IPV4, "127.0.0.1", 8080);
    LogDebug("Client connecting to server...");
    auto connectResult = co_await client.connect(serverHost);
    if (!connectResult) {
        LogError("Client: Failed to connect: {}", connectResult.error().message());
        co_return;
    }

    LogInfo("Client: Connected to server");

    // 发送消息
    const char* msg = "Hello, Server!";
    auto sendResult = co_await client.send(msg, strlen(msg));
    if (!sendResult) {
        LogError("Client: Send failed");
        co_return;
    }

    LogInfo("Client: Sent message");

    // 接收回复
    char buffer[1024];
    auto recvResult = co_await client.recv(buffer, sizeof(buffer));
    if (!recvResult) {
        LogError("Client: Recv failed");
        co_return;
    }

    auto& bytes = recvResult.value();
    LogInfo("Client: Received echo: {}", bytes.toStringView());

    co_await client.close();
    LogInfo("Client stopped");
    co_return;
}

int main() {
    LogInfo("TcpSocket Test");

#ifdef USE_KQUEUE
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
    scheduler.start();
    LogDebug("Scheduler started");

    // 启动服务器
    scheduler.spawn(echoServer(&scheduler));
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(echoClient(&scheduler));
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
    scheduler.spawn(echoServer(&scheduler));
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(echoClient(&scheduler));
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
    scheduler.spawn(echoServer(&scheduler));
    LogDebug("Server coroutine spawned");

    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 启动客户端
    scheduler.spawn(echoClient(&scheduler));
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
