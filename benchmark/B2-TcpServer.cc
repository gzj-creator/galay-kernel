#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <csignal>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/IOUringScheduler.h"
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
#endif

using namespace galay::async;
using namespace galay::kernel;

std::atomic<uint64_t> g_total_connections{0};
std::atomic<uint64_t> g_total_bytes{0};
std::atomic<uint64_t> g_total_requests{0};
std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    g_running.store(false, std::memory_order_release);
}

// 处理单个客户端连接
Coroutine handleClient(GHandle clientHandle) {
    TcpSocket client(clientHandle);
    client.option().handleNonBlock();

    char buffer[4096];

    while (g_running.load(std::memory_order_relaxed)) {
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult) {
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            break;
        }

        g_total_bytes.fetch_add(bytes.size(), std::memory_order_relaxed);
        g_total_requests.fetch_add(1, std::memory_order_relaxed);

        // Echo back
        auto sendResult = co_await client.send(bytes.c_str(), bytes.size());
        if (!sendResult) {
            break;
        }
        g_total_bytes.fetch_add(sendResult.value(), std::memory_order_relaxed);
    }

    co_await client.close();
    co_return;
}

// 接受连接的协程
Coroutine acceptLoop(IOScheduler* scheduler, TcpSocket* listener) {
    while (g_running.load(std::memory_order_relaxed)) {
        Host clientHost;
        auto acceptResult = co_await listener->accept(&clientHost);
        if (!acceptResult) {
            if (g_running.load(std::memory_order_relaxed)) {
                LogError("Accept failed: {}", acceptResult.error().message());
            }
            continue;
        }

        g_total_connections.fetch_add(1, std::memory_order_relaxed);

        // 启动处理协程
        scheduler->spawn(handleClient(acceptResult.value()));
    }
    co_return;
}

// 统计打印线程
void statsThread() {
    auto lastTime = std::chrono::steady_clock::now();
    uint64_t lastBytes = 0;
    uint64_t lastRequests = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        uint64_t currentBytes = g_total_bytes.load(std::memory_order_relaxed);
        uint64_t currentRequests = g_total_requests.load(std::memory_order_relaxed);
        uint64_t connections = g_total_connections.load(std::memory_order_relaxed);

        double bytesPerSec = (currentBytes - lastBytes) * 1000.0 / elapsed;
        double requestsPerSec = (currentRequests - lastRequests) * 1000.0 / elapsed;

        std::cout << "[Stats] Connections: " << connections
                  << " | Requests/s: " << static_cast<uint64_t>(requestsPerSec)
                  << " | Throughput: " << (bytesPerSec / 1024 / 1024) << " MB/s"
                  << " | Total Requests: " << currentRequests
                  << std::endl;

        lastTime = now;
        lastBytes = currentBytes;
        lastRequests = currentRequests;
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    LogInfo("Benchmark Server starting on port {}", port);

#if defined(USE_KQUEUE)
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
#elif defined(USE_IOURING)
    LogInfo("Using IOUringScheduler (Linux io_uring)");
    IOUringScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler (Linux epoll)");
    EpollScheduler scheduler;
#else
    LogWarn("No supported IO backend available");
    return 1;
#endif

    scheduler.start();

    TcpSocket listener;

    listener.option().handleReuseAddr();
    listener.option().handleReusePort();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "0.0.0.0", port);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        return 1;
    }

    auto listenResult = listener.listen(1024);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        return 1;
    }

    LogInfo("Server listening on 0.0.0.0:{}", port);
    LogInfo("Press Ctrl+C to stop");

    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 启动统计线程
    std::thread stats(statsThread);

    // 启动接受连接协程
    scheduler.spawn(acceptLoop(&scheduler, &listener));

    // 等待信号
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    scheduler.stop();
    stats.join();

    LogInfo("Server stopped. Total connections: {}, Total requests: {}",
            g_total_connections.load(), g_total_requests.load());

    return 0;
}
