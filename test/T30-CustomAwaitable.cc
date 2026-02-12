/**
 * @file T30-CustomAwaitable.cc
 * @brief 测试 CustomAwaitable 继承模式：
 *        定义 SendThenRecvAwaitable 继承 CustomAwaitable，
 *        构造时填充 SEND → RECV 队列，await_resume 返回 RECV 结果。
 */

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
#include <string>

using namespace galay::kernel;

std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};

// ==================== 用户自定义 Awaitable ====================

/**
 * 先 SEND 再 RECV，一个 co_await 完成两步 IO。
 * await_resume 返回 RECV 的结果。
 */
struct SendThenRecvAwaitable : public CustomAwaitable {
    SendIOContext m_send;
    RecvIOContext m_recv;

    SendThenRecvAwaitable(IOController* ctrl,
                          const char* sendData, size_t sendLen,
                          char* recvBuf, size_t recvBufLen)
        : CustomAwaitable(ctrl)
        , m_send(sendData, sendLen)
        , m_recv(recvBuf, recvBufLen)
    {
        addTask(IOEventType::SEND, &m_send);
        addTask(IOEventType::RECV, &m_recv);
    }

    bool await_ready() { return false; }

    std::expected<Bytes, IOError> await_resume() {
        onCompleted();
        return std::move(m_recv.m_result);
    }

    /// 获取 SEND 结果
    const auto& sendResult() const { return m_send.m_result; }
};

// ==================== 测试协程 ====================

Coroutine serverCoroutine(IOScheduler* scheduler, int listen_fd)
{
    g_total++;

    IOController listen_ctrl(GHandle{.fd = listen_fd});
    Host clientHost;
    AcceptAwaitable acceptAw(&listen_ctrl, &clientHost);
    auto acceptResult = co_await acceptAw;
    if (!acceptResult) {
        LogError("[Server] Accept failed: {}", acceptResult.error().message());
        g_failed++;
        co_return;
    }

    int client_fd = acceptResult.value().fd;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    LogInfo("[Server] Client connected, fd={}", client_fd);

    // 一个 co_await 完成 SEND "hello" + RECV 回复
    IOController ctrl(GHandle{.fd = client_fd});
    const std::string greeting = "hello";
    char recvBuf[256]{};

    SendThenRecvAwaitable custom(&ctrl, greeting.c_str(), greeting.size(),
                                  recvBuf, sizeof(recvBuf) - 1);

    LogInfo("[Server] co_await SendThenRecvAwaitable...");
    auto recvResult = co_await custom;
    LogInfo("[Server] SendThenRecvAwaitable completed");

    // 检查 SEND
    bool sendOk = false;
    if (custom.sendResult().has_value()) {
        LogInfo("[Server] SEND: {} bytes", custom.sendResult().value());
        sendOk = (custom.sendResult().value() == greeting.size());
    } else {
        LogError("[Server] SEND failed: {}", custom.sendResult().error().message());
    }

    // 检查 RECV
    bool recvOk = false;
    if (recvResult.has_value()) {
        std::string received(reinterpret_cast<const char*>(recvResult->data()),
                             recvResult->size());
        LogInfo("[Server] RECV: \"{}\"", received);
        recvOk = (received == "world");
    } else {
        LogError("[Server] RECV failed: {}", recvResult.error().message());
    }

    if (sendOk && recvOk) {
        LogInfo("[Server] PASS");
        g_passed++;
    } else {
        LogError("[Server] FAIL (sendOk={}, recvOk={})", sendOk, recvOk);
        g_failed++;
    }

    close(client_fd);
    co_return;
}

Coroutine clientCoroutine(IOScheduler* scheduler, const char* ip, int port)
{
    g_total++;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LogError("[Client] Socket creation failed");
        g_failed++;
        co_return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    IOController ctrl(GHandle{.fd = fd});

    ConnectAwaitable connectAw(&ctrl, Host(IPType::IPV4, ip, port));
    auto connResult = co_await connectAw;
    if (!connResult) {
        LogError("[Client] Connect failed: {}", connResult.error().message());
        close(fd);
        g_failed++;
        co_return;
    }
    LogInfo("[Client] Connected");

    // RECV 服务端的 greeting
    char buf[256]{};
    RecvAwaitable recvAw(&ctrl, buf, sizeof(buf) - 1);
    auto recvResult = co_await recvAw;
    if (!recvResult) {
        LogError("[Client] Recv failed: {}", recvResult.error().message());
        close(fd);
        g_failed++;
        co_return;
    }
    std::string received(reinterpret_cast<const char*>(recvResult->data()), recvResult->size());
    LogInfo("[Client] Received: \"{}\"", received);

    // SEND 回复
    const std::string reply = "world";
    SendAwaitable sendAw(&ctrl, reply.c_str(), reply.size());
    auto sendResult = co_await sendAw;
    if (!sendResult) {
        LogError("[Client] Send failed: {}", sendResult.error().message());
        close(fd);
        g_failed++;
        co_return;
    }
    LogInfo("[Client] Sent: \"{}\" ({} bytes)", reply, sendResult.value());

    if (received == "hello") {
        LogInfo("[Client] PASS");
        g_passed++;
    } else {
        LogError("[Client] FAIL: expected \"hello\", got \"{}\"", received);
        g_failed++;
    }

    close(fd);
    co_return;
}

int main()
{
    LogInfo("========================================");
    LogInfo("CustomAwaitable Inheritance Test");
    LogInfo("  SendThenRecvAwaitable: SEND + RECV");
    LogInfo("========================================");

#ifdef USE_IOURING
    LogInfo("Backend: io_uring");
#elif defined(USE_EPOLL)
    LogInfo("Backend: epoll");
#elif defined(USE_KQUEUE)
    LogInfo("Backend: kqueue");
#endif

    const int PORT = 20030;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LogError("Failed to create listen socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    scheduler.spawn(serverCoroutine(&scheduler, listen_fd));
    scheduler.spawn(clientCoroutine(&scheduler, "127.0.0.1", PORT));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    scheduler.stop();
    close(listen_fd);

    galay::test::TestResultWriter writer("test_custom_awaitable");
    for (int i = 0; i < g_total.load(); ++i) writer.addTest();
    for (int i = 0; i < g_passed.load(); ++i) writer.addPassed();
    for (int i = 0; i < g_failed.load(); ++i) writer.addFailed();
    writer.writeResult();

    LogInfo("========================================");
    LogInfo("Test Results: Total={}, Passed={}, Failed={}",
            g_total.load(), g_passed.load(), g_failed.load());
    LogInfo("========================================");

    return g_failed > 0 ? 1 : 0;
}
