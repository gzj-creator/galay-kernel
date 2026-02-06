/**
 * T26-ConcurrentRecvSend
 *
 * 测试同一个 TcpSocket 上同时进行 recv 和 send（全双工）。
 *
 * 架构：
 *   Server accept 后，对同一个 client socket 同时 spawn 读协程和写协程。
 *   Client connect 后，也对同一个 socket 同时 spawn 读协程和写协程。
 *   写协程每轮发送后 sleep 一小段时间，确保消息不会全部合并到一个 TCP 段。
 *   读协程累计接收字节数，达到预期总字节数即视为成功。
 *
 * 预期：所有三个后端（kqueue / epoll / io_uring）均通过。
 */

#include <iostream>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
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

static std::atomic<bool> g_server_ready{false};
static std::atomic<int>  g_server_recv_bytes{0};
static std::atomic<int>  g_server_send_rounds{0};
static std::atomic<int>  g_client_recv_bytes{0};
static std::atomic<int>  g_client_send_rounds{0};

constexpr int kRounds = 5;
constexpr int kPort   = 19876;
// 每条消息固定 8 字节: "C2S-XXXX" 或 "S2C-XXXX"
constexpr int kMsgLen = 8;
constexpr int kExpectedBytes = kRounds * kMsgLen;

// 使用共享指针管理 socket 生命周期
static std::shared_ptr<TcpSocket> g_server_client;
static std::shared_ptr<TcpSocket> g_server_listener;
static std::shared_ptr<TcpSocket> g_client_sock;

// ==================== Server ====================

Coroutine serverRecvLoop(std::shared_ptr<TcpSocket> client) {
    char buffer[256];
    int totalBytes = 0;
    while (totalBytes < kExpectedBytes) {
        auto result = co_await client->recv(buffer, sizeof(buffer));
        if (!result) {
            LogError("[S-Recv] failed after {} bytes: {}", totalBytes, result.error().message());
            co_return;
        }
        int n = static_cast<int>(result.value().size());
        totalBytes += n;
        LogInfo("[S-Recv] got {} bytes (total: {}/{})", n, totalBytes, kExpectedBytes);
        g_server_recv_bytes.store(totalBytes);
    }
    co_return;
}

Coroutine serverSendLoop(std::shared_ptr<TcpSocket> client) {
    for (int i = 0; i < kRounds; ++i) {
        char msg[kMsgLen + 1];
        snprintf(msg, sizeof(msg), "S2C-%04d", i);
        auto result = co_await client->send(msg, kMsgLen);
        if (!result) {
            LogError("[S-Send] round {} failed: {}", i, result.error().message());
            co_return;
        }
        LogInfo("[S-Send] round {}: sent {} bytes", i, result.value());
        g_server_send_rounds.fetch_add(1);
    }
    co_return;
}

Coroutine serverMain(IOScheduler* scheduler) {
    g_server_listener = std::make_shared<TcpSocket>();
    g_server_listener->option().handleReuseAddr();
    g_server_listener->option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", kPort);
    auto bindResult = g_server_listener->bind(bindHost);
    if (!bindResult) {
        LogError("[Server] bind failed: {}", bindResult.error().message());
        co_return;
    }
    auto listenResult = g_server_listener->listen(128);
    if (!listenResult) {
        LogError("[Server] listen failed: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("[Server] listening on 127.0.0.1:{}", kPort);
    g_server_ready.store(true);

    Host clientHost;
    auto acceptResult = co_await g_server_listener->accept(&clientHost);
    if (!acceptResult) {
        LogError("[Server] accept failed: {}", acceptResult.error().message());
        co_return;
    }

    LogInfo("[Server] client connected from {}:{}", clientHost.ip(), clientHost.port());

    g_server_client = std::make_shared<TcpSocket>(acceptResult.value());
    g_server_client->option().handleNonBlock();

    // 关键：同一个 socket 同时 spawn 读和写
    scheduler->spawn(serverRecvLoop(g_server_client));
    scheduler->spawn(serverSendLoop(g_server_client));
    co_return;
}

// ==================== Client ====================

Coroutine clientRecvLoop(std::shared_ptr<TcpSocket> sock) {
    char buffer[256];
    int totalBytes = 0;
    while (totalBytes < kExpectedBytes) {
        auto result = co_await sock->recv(buffer, sizeof(buffer));
        if (!result) {
            LogError("[C-Recv] failed after {} bytes: {}", totalBytes, result.error().message());
            co_return;
        }
        int n = static_cast<int>(result.value().size());
        totalBytes += n;
        LogInfo("[C-Recv] got {} bytes (total: {}/{})", n, totalBytes, kExpectedBytes);
        g_client_recv_bytes.store(totalBytes);
    }
    co_return;
}

Coroutine clientSendLoop(std::shared_ptr<TcpSocket> sock) {
    for (int i = 0; i < kRounds; ++i) {
        char msg[kMsgLen + 1];
        snprintf(msg, sizeof(msg), "C2S-%04d", i);
        auto result = co_await sock->send(msg, kMsgLen);
        if (!result) {
            LogError("[C-Send] round {} failed: {}", i, result.error().message());
            co_return;
        }
        LogInfo("[C-Send] round {}: sent {} bytes", i, result.value());
        g_client_send_rounds.fetch_add(1);
    }
    co_return;
}

Coroutine clientMain(IOScheduler* scheduler) {
    while (!g_server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    g_client_sock = std::make_shared<TcpSocket>();
    g_client_sock->option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", kPort);
    auto connectResult = co_await g_client_sock->connect(serverHost);
    if (!connectResult) {
        LogError("[Client] connect failed: {}", connectResult.error().message());
        co_return;
    }

    LogInfo("[Client] connected");

    // 关键：同一个 socket 同时 spawn 读和写
    scheduler->spawn(clientRecvLoop(g_client_sock));
    scheduler->spawn(clientSendLoop(g_client_sock));
    co_return;
}

// ==================== main ====================

int main() {
    LogInfo("=== T26: Concurrent Recv+Send on same socket ===");

#ifdef USE_KQUEUE
    LogInfo("Backend: KqueueScheduler");
    KqueueScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Backend: EpollScheduler");
    EpollScheduler scheduler;
#elif defined(USE_IOURING)
    LogInfo("Backend: IOUringScheduler");
    IOUringScheduler scheduler;
#else
    LogWarn("No supported scheduler");
    return 1;
#endif

    scheduler.start();

    scheduler.spawn(serverMain(&scheduler));
    scheduler.spawn(clientMain(&scheduler));

    // 在 main 线程等待
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        if (g_server_recv_bytes.load() >= kExpectedBytes &&
            g_server_send_rounds.load() >= kRounds &&
            g_client_recv_bytes.load() >= kExpectedBytes &&
            g_client_send_rounds.load() >= kRounds)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    scheduler.stop();

    g_server_client.reset();
    g_server_listener.reset();
    g_client_sock.reset();

    LogInfo("=== Results ===");
    LogInfo("  Server recv bytes: {}/{}", g_server_recv_bytes.load(), kExpectedBytes);
    LogInfo("  Server send rounds: {}/{}", g_server_send_rounds.load(), kRounds);
    LogInfo("  Client recv bytes: {}/{}", g_client_recv_bytes.load(), kExpectedBytes);
    LogInfo("  Client send rounds: {}/{}", g_client_send_rounds.load(), kRounds);

    bool passed = (g_server_recv_bytes.load() >= kExpectedBytes) &&
                  (g_server_send_rounds.load() >= kRounds) &&
                  (g_client_recv_bytes.load() >= kExpectedBytes) &&
                  (g_client_send_rounds.load() >= kRounds);

    if (passed) {
        LogInfo("=== TEST PASSED ===");
        return 0;
    } else {
        LogError("=== TEST FAILED ===");
        return 1;
    }
}
