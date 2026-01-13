#include <iostream>
#include <cstring>
#include <atomic>
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

std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_test_passed{false};

// 服务器协程 - 使用 readv 接收数据
Coroutine readvServer(IOScheduler* scheduler) {
    LogInfo("[Server] Starting...");
    TcpSocket listener;

    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9090);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("[Server] Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("[Server] Failed to listen: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("[Server] Listening on 127.0.0.1:9090");
    g_server_ready = true;

    Host clientHost;
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("[Server] Failed to accept: {}", acceptResult.error().message());
        co_return;
    }

    LogInfo("[Server] Client connected from {}:{}", clientHost.ip(), clientHost.port());

    TcpSocket client(acceptResult.value());
    client.option().handleNonBlock();

    // 使用 readv 接收数据到多个缓冲区
    char header[16];
    char body[64];
    std::memset(header, 0, sizeof(header));
    std::memset(body, 0, sizeof(body));

    std::vector<struct iovec> iovecs(2);
    iovecs[0].iov_base = header;
    iovecs[0].iov_len = sizeof(header);
    iovecs[1].iov_base = body;
    iovecs[1].iov_len = sizeof(body);

    LogInfo("[Server] Waiting for readv...");
    auto readvResult = co_await client.readv(std::move(iovecs));
    if (!readvResult) {
        LogError("[Server] readv failed: {}", readvResult.error().message());
        co_await client.close();
        co_await listener.close();
        co_return;
    }

    size_t totalRead = readvResult.value();
    LogInfo("[Server] readv received {} bytes", totalRead);
    LogInfo("[Server] Header: '{}'", std::string(header, std::min(totalRead, sizeof(header))));

    if (totalRead > sizeof(header)) {
        LogInfo("[Server] Body: '{}'", std::string(body, totalRead - sizeof(header)));
    }

    // 验证数据
    bool headerOk = (std::strncmp(header, "HEADER:12345678", 15) == 0);
    bool bodyOk = (totalRead > sizeof(header)) &&
                  (std::strncmp(body, "BODY:Hello World from writev!", 29) == 0);

    if (headerOk && bodyOk) {
        LogInfo("[Server] Data verification PASSED!");
        g_test_passed = true;
    } else {
        LogError("[Server] Data verification FAILED!");
        LogError("[Server] headerOk={}, bodyOk={}", headerOk, bodyOk);
    }

    // 使用 writev 发送响应
    const char* respHeader = "RESP:OK_________";  // 16 bytes
    const char* respBody = "Response from server using writev!";

    std::vector<struct iovec> respIovecs(2);
    respIovecs[0].iov_base = const_cast<char*>(respHeader);
    respIovecs[0].iov_len = 16;
    respIovecs[1].iov_base = const_cast<char*>(respBody);
    respIovecs[1].iov_len = strlen(respBody);

    auto writevResult = co_await client.writev(std::move(respIovecs));
    if (!writevResult) {
        LogError("[Server] writev failed: {}", writevResult.error().message());
    } else {
        LogInfo("[Server] writev sent {} bytes", writevResult.value());
    }

    co_await client.close();
    co_await listener.close();
    LogInfo("[Server] Stopped");
    co_return;
}

// 客户端协程 - 使用 writev 发送数据
Coroutine writevClient(IOScheduler* scheduler) {
    // 等待服务器就绪
    while (!g_server_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LogInfo("[Client] Starting...");
    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9090);
    auto connectResult = co_await client.connect(serverHost);
    if (!connectResult) {
        LogError("[Client] Failed to connect: {}", connectResult.error().message());
        co_return;
    }

    LogInfo("[Client] Connected to server");

    // 使用 writev 发送多个缓冲区
    const char* header = "HEADER:12345678";  // 15 bytes + 1 padding = 16 bytes
    const char* body = "BODY:Hello World from writev!";

    std::vector<struct iovec> iovecs(2);
    iovecs[0].iov_base = const_cast<char*>(header);
    iovecs[0].iov_len = 16;  // 固定16字节
    iovecs[1].iov_base = const_cast<char*>(body);
    iovecs[1].iov_len = strlen(body);

    LogInfo("[Client] Sending with writev...");
    auto writevResult = co_await client.writev(std::move(iovecs));
    if (!writevResult) {
        LogError("[Client] writev failed: {}", writevResult.error().message());
        co_await client.close();
        co_return;
    }

    LogInfo("[Client] writev sent {} bytes", writevResult.value());

    // 使用 readv 接收响应
    char respHeader[16];
    char respBody[64];
    std::memset(respHeader, 0, sizeof(respHeader));
    std::memset(respBody, 0, sizeof(respBody));

    std::vector<struct iovec> respIovecs(2);
    respIovecs[0].iov_base = respHeader;
    respIovecs[0].iov_len = sizeof(respHeader);
    respIovecs[1].iov_base = respBody;
    respIovecs[1].iov_len = sizeof(respBody);

    LogInfo("[Client] Waiting for readv response...");
    auto readvResult = co_await client.readv(std::move(respIovecs));
    if (!readvResult) {
        LogError("[Client] readv failed: {}", readvResult.error().message());
    } else {
        size_t totalRead = readvResult.value();
        LogInfo("[Client] readv received {} bytes", totalRead);
        LogInfo("[Client] Response Header: '{}'", std::string(respHeader, std::min(totalRead, sizeof(respHeader))));
        if (totalRead > sizeof(respHeader)) {
            LogInfo("[Client] Response Body: '{}'", std::string(respBody, totalRead - sizeof(respHeader)));
        }
    }

    co_await client.close();
    LogInfo("[Client] Stopped");
    co_return;
}

int main() {
    LogInfo("=== TcpSocket readv/writev Test ===");

#ifdef USE_KQUEUE
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler (Linux)");
    EpollScheduler scheduler;
#elif defined(USE_IOURING)
    LogInfo("Using IOUringScheduler (Linux io_uring)");
    IOUringScheduler scheduler;
#else
    LogWarn("No supported scheduler available");
    return 1;
#endif

    scheduler.start();

    // 启动服务器
    scheduler.spawn(readvServer(&scheduler));

    // 启动客户端
    scheduler.spawn(writevClient(&scheduler));

    // 等待测试完成
    std::this_thread::sleep_for(std::chrono::seconds(3));

    scheduler.stop();

    if (g_test_passed) {
        LogInfo("=== TEST PASSED ===");
        return 0;
    } else {
        LogError("=== TEST FAILED ===");
        return 1;
    }
}
