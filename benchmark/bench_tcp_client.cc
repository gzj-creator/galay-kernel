#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
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

std::atomic<uint64_t> g_total_requests{0};
std::atomic<uint64_t> g_total_bytes{0};
std::atomic<uint64_t> g_success_count{0};
std::atomic<uint64_t> g_error_count{0};
std::atomic<bool> g_running{true};

struct BenchConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    int connections = 100;
    int messageSize = 256;
    int duration = 10;  // seconds
};

// 单个客户端连接的压测协程
Coroutine benchClient(IOScheduler* scheduler, const BenchConfig& config, int clientId) {
    TcpSocket client(scheduler);

    auto createResult = client.create(IPType::IPV4);
    if (!createResult) {
        g_error_count.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, config.host, config.port);
    auto connectResult = co_await client.connect(serverHost);
    if (!connectResult) {
        g_error_count.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    // 准备测试数据
    std::string message(config.messageSize, 'X');
    char recvBuffer[8192];

    while (g_running.load(std::memory_order_relaxed)) {
        // 发送请求
        auto sendResult = co_await client.send(message.data(), message.size());
        if (!sendResult) {
            g_error_count.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        // 接收响应
        auto recvResult = co_await client.recv(recvBuffer, sizeof(recvBuffer));
        if (!recvResult) {
            g_error_count.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            break;
        }

        g_total_requests.fetch_add(1, std::memory_order_relaxed);
        g_total_bytes.fetch_add(bytes.size() + message.size(), std::memory_order_relaxed);
        g_success_count.fetch_add(1, std::memory_order_relaxed);
    }

    co_await client.close();
    co_return;
}

// 统计打印线程
void statsThread(const BenchConfig& config) {
    auto startTime = std::chrono::steady_clock::now();
    auto lastTime = startTime;
    uint64_t lastRequests = 0;
    uint64_t lastBytes = 0;

    int elapsed_seconds = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed_seconds++;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        uint64_t currentRequests = g_total_requests.load(std::memory_order_relaxed);
        uint64_t currentBytes = g_total_bytes.load(std::memory_order_relaxed);
        uint64_t errors = g_error_count.load(std::memory_order_relaxed);

        double requestsPerSec = (currentRequests - lastRequests) * 1000.0 / elapsed;
        double bytesPerSec = (currentBytes - lastBytes) * 1000.0 / elapsed;

        std::cout << "[" << elapsed_seconds << "s] "
                  << "QPS: " << static_cast<uint64_t>(requestsPerSec)
                  << " | Throughput: " << (bytesPerSec / 1024 / 1024) << " MB/s"
                  << " | Total: " << currentRequests
                  << " | Errors: " << errors
                  << std::endl;

        lastTime = now;
        lastRequests = currentRequests;
        lastBytes = currentBytes;

        // 检查是否达到测试时间
        if (elapsed_seconds >= config.duration) {
            g_running.store(false, std::memory_order_release);
            break;
        }
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -h <host>        Server host (default: 127.0.0.1)\n"
              << "  -p <port>        Server port (default: 8080)\n"
              << "  -c <connections> Number of concurrent connections (default: 100)\n"
              << "  -s <size>        Message size in bytes (default: 256)\n"
              << "  -d <duration>    Test duration in seconds (default: 10)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    BenchConfig config;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            config.host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config.connections = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config.messageSize = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            config.duration = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "=== Benchmark Client ===" << std::endl;
    std::cout << "Target: " << config.host << ":" << config.port << std::endl;
    std::cout << "Connections: " << config.connections << std::endl;
    std::cout << "Message Size: " << config.messageSize << " bytes" << std::endl;
    std::cout << "Duration: " << config.duration << " seconds" << std::endl;
    std::cout << "========================" << std::endl;

#if defined(USE_KQUEUE)
    std::cout << "Using KqueueScheduler (macOS)" << std::endl;
    KqueueScheduler scheduler;
#elif defined(USE_IOURING)
    std::cout << "Using IOUringScheduler (Linux io_uring)" << std::endl;
    IOUringScheduler scheduler;
#elif defined(USE_EPOLL)
    std::cout << "Using EpollScheduler (Linux epoll)" << std::endl;
    EpollScheduler scheduler;
#else
    LogWarn("No supported IO backend available");
    return 1;
#endif

    scheduler.start();

    // 启动统计线程
    std::thread stats(statsThread, std::ref(config));

    // 启动所有客户端连接
    std::cout << "Starting " << config.connections << " connections..." << std::endl;
    for (int i = 0; i < config.connections; i++) {
        scheduler.spawn(benchClient(&scheduler, config, i));
    }

    // 等待统计线程结束
    stats.join();

    // 等待一下让所有协程完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    scheduler.stop();

    // 打印最终结果
    std::cout << "\n=== Final Results ===" << std::endl;
    std::cout << "Total Requests: " << g_total_requests.load() << std::endl;
    std::cout << "Successful: " << g_success_count.load() << std::endl;
    std::cout << "Errors: " << g_error_count.load() << std::endl;
    std::cout << "Total Data: " << (g_total_bytes.load() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "Average QPS: " << (g_total_requests.load() / config.duration) << std::endl;
    std::cout << "Average Throughput: " << (g_total_bytes.load() / config.duration / 1024.0 / 1024.0) << " MB/s" << std::endl;

    return 0;
}
