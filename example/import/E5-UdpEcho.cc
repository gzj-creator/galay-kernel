import galay.kernel;

#include <coroutine>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

using namespace galay::async;
using namespace galay::kernel;

namespace {
constexpr uint16_t kPort = 9095;
std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_server_done{false};
std::atomic<bool> g_client_done{false};

Coroutine udpServer() {
    UdpSocket socket;

    auto optResult = socket.option().handleReuseAddr();
    if (!optResult) {
        g_server_done.store(true, std::memory_order_release);
        co_return;
    }

    optResult = socket.option().handleNonBlock();
    if (!optResult) {
        g_server_done.store(true, std::memory_order_release);
        co_return;
    }

    auto bindResult = socket.bind(Host(IPType::IPV4, "127.0.0.1", kPort));
    if (!bindResult) {
        g_server_done.store(true, std::memory_order_release);
        co_return;
    }

    g_server_ready.store(true, std::memory_order_release);

    char buffer[1024];
    Host peer;
    auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &peer);
    if (recvResult && recvResult.value().size() > 0) {
        auto& bytes = recvResult.value();
        auto sendResult = co_await socket.sendto(bytes.c_str(), bytes.size(), peer);
        if (!sendResult) {
            std::cerr << "udp sendto failed\n";
        }
    }

    co_await socket.close();
    g_server_done.store(true, std::memory_order_release);
}

Coroutine udpClient() {
    while (!g_server_ready.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    UdpSocket socket;
    auto optResult = socket.option().handleNonBlock();
    if (!optResult) {
        g_client_done.store(true, std::memory_order_release);
        co_return;
    }

    std::string message = "hello from import udp client";
    Host server(IPType::IPV4, "127.0.0.1", kPort);
    auto sendResult = co_await socket.sendto(message.c_str(), message.size(), server);
    if (!sendResult) {
        co_await socket.close();
        g_client_done.store(true, std::memory_order_release);
        co_return;
    }

    char buffer[1024];
    Host peer;
    auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &peer);
    if (recvResult && recvResult.value().size() > 0) {
        std::cout << "udp response: " << recvResult.value().toStringView() << "\n";
    }

    co_await socket.close();
    g_client_done.store(true, std::memory_order_release);
}
}  // namespace

int main() {
    Runtime runtime(1, 1);
    runtime.start();

    auto* io = runtime.getNextIOScheduler();
    io->spawn(udpServer());
    io->spawn(udpClient());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!g_server_done.load(std::memory_order_acquire) ||
            !g_client_done.load(std::memory_order_acquire)) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    runtime.stop();
    return 0;
}
