#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/common/Host.hpp"
#include "galay-kernel/common/Log.h"
#include "test_result_writer.h"

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
using TestScheduler = galay::kernel::IOUringScheduler;
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
using TestScheduler = galay::kernel::EpollScheduler;
#elif defined(USE_KQUEUE)
#include "galay-kernel/kernel/KqueueScheduler.h"
using TestScheduler = galay::kernel::KqueueScheduler;
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <thread>

using namespace galay::kernel;

std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};

// 测试 RecvNotifyAwaitable - 服务端
Coroutine testRecvNotifyServer(IOScheduler* scheduler, int listen_fd)
{
    g_total++;
    LogInfo("[Server] Waiting for connection...");

    // 使用 AcceptAwaitable 等待连接
    IOController listen_controller(GHandle{.fd = listen_fd});
    Host clientHost;
    AcceptAwaitable acceptAwaitable(&listen_controller, &clientHost);
    auto acceptResult = co_await acceptAwaitable;
    if (!acceptResult) {
        LogError("[Server] Accept failed: {}", acceptResult.error().message());
        g_failed++;
        co_return;
    }

    int client_fd = acceptResult.value().fd;

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    LogInfo("[Server] Client connected, fd={}", client_fd);

    // 创建 IOController
    IOController controller(GHandle{.fd = client_fd});

    // 使用 RecvNotifyAwaitable 等待可读
    RecvNotifyAwaitable recvNotify(&controller);
    LogInfo("[Server] Waiting for recv notify...");
    co_await recvNotify;
    LogInfo("[Server] Recv notify received!");

    // 手动执行 recv
    char buffer[256];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        LogInfo("[Server] Received: {}", buffer);
        g_passed++;
    } else {
        LogError("[Server] Recv failed after notify");
        g_failed++;
    }

    close(client_fd);
    co_return;
}

// 测试 SendNotifyAwaitable - 客户端
Coroutine testSendNotifyClient(IOScheduler* scheduler, const char* server_ip, int server_port)
{
    g_total++;

    // 等待服务端启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 创建 socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LogError("[Client] Socket creation failed");
        g_failed++;
        co_return;
    }

    // 设置非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // 连接服务器
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    int ret = connect(fd, (sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        LogError("[Client] Connect failed: {}", strerror(errno));
        close(fd);
        g_failed++;
        co_return;
    }

    // 创建 IOController
    IOController controller(GHandle{.fd = fd});

    // 使用 SendNotifyAwaitable 等待可写（连接完成）
    SendNotifyAwaitable sendNotify(&controller);
    LogInfo("[Client] Waiting for send notify (connect completion)...");
    co_await sendNotify;
    LogInfo("[Client] Send notify received!");

    // 检查连接是否成功
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        LogError("[Client] Connect failed: {}", strerror(error));
        close(fd);
        g_failed++;
        co_return;
    }

    // 手动执行 send
    const char* msg = "Hello from SendNotify test!";
    ssize_t n = send(fd, msg, strlen(msg), 0);
    if (n > 0) {
        LogInfo("[Client] Sent {} bytes", n);
        g_passed++;
    } else {
        LogError("[Client] Send failed after notify");
        g_failed++;
    }

    close(fd);
    co_return;
}

int main()
{
    LogInfo("========================================");
    LogInfo("RecvNotify/SendNotify Awaitable Test");
    LogInfo("========================================");

#ifdef USE_IOURING
    LogInfo("Backend: io_uring");
#elif defined(USE_EPOLL)
    LogInfo("Backend: epoll");
#elif defined(USE_KQUEUE)
    LogInfo("Backend: kqueue");
#endif

    const int PORT = 18888;

    // 创建监听 socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LogError("Failed to create listen socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 设置非阻塞
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LogError("Bind failed: {}", strerror(errno));
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0) {
        LogError("Listen failed: {}", strerror(errno));
        close(listen_fd);
        return 1;
    }

    LogInfo("Server listening on port {}", PORT);

    TestScheduler scheduler;
    scheduler.start();

    // 启动服务端协程
    scheduler.spawn(testRecvNotifyServer(&scheduler, listen_fd));

    // 启动客户端协程
    scheduler.spawn(testSendNotifyClient(&scheduler, "127.0.0.1", PORT));

    // 等待测试完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    scheduler.stop();
    close(listen_fd);

    // 写入测试结果
    galay::test::TestResultWriter writer("test_notify_awaitable");
    for (int i = 0; i < g_total.load(); ++i) {
        writer.addTest();
    }
    for (int i = 0; i < g_passed.load(); ++i) {
        writer.addPassed();
    }
    for (int i = 0; i < g_failed.load(); ++i) {
        writer.addFailed();
    }
    writer.writeResult();

    LogInfo("========================================");
    LogInfo("Test Results: Total={}, Passed={}, Failed={}",
            g_total.load(), g_passed.load(), g_failed.load());
    LogInfo("========================================");

    return g_failed > 0 ? 1 : 0;
}
