#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/common/Host.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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

using namespace galay::kernel;

namespace {

std::atomic<bool> g_server_ok{false};
std::atomic<bool> g_client_ok{false};

struct SendThenRecvAwaitable : public CustomSequenceAwaitable {
    SendIOContext m_send;
    RecvIOContext m_recv;

    SendThenRecvAwaitable(IOController* controller,
                          const char* send_data,
                          size_t send_len,
                          char* recv_buffer,
                          size_t recv_len)
        : CustomSequenceAwaitable(controller)
        , m_send(send_data, send_len)
        , m_recv(recv_buffer, recv_len)
    {
        addSteps(m_send, m_recv);
    }

    std::expected<size_t, IOError> await_resume() {
        return complete(std::move(m_recv.m_result));
    }

    const auto& sendResult() const {
        return m_send.m_result;
    }
};

Coroutine serverTask(int listen_fd) {
    IOController listen_ctrl(GHandle{.fd = listen_fd});
    Host client_host;
    AcceptAwaitable accept_awaitable(&listen_ctrl, &client_host);
    auto accepted = co_await accept_awaitable;
    if (!accepted) {
        co_return;
    }

    int client_fd = accepted->fd;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    IOController controller(GHandle{.fd = client_fd});
    char recv_buffer[64]{};
    const std::string greeting = "hello";
    SendThenRecvAwaitable exchange(&controller,
                                   greeting.c_str(),
                                   greeting.size(),
                                   recv_buffer,
                                   sizeof(recv_buffer) - 1);

    auto received = co_await exchange;
    if (exchange.sendResult() && received) {
        const std::string reply(recv_buffer, received.value());
        if (exchange.sendResult().value() == greeting.size() && reply == "world") {
            g_server_ok.store(true, std::memory_order_release);
        }
    }

    close(client_fd);
}

Coroutine clientTask(const char* ip, int port) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        co_return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    IOController controller(GHandle{.fd = fd});
    ConnectAwaitable connect_awaitable(&controller, Host(IPType::IPV4, ip, port));
    auto connected = co_await connect_awaitable;
    if (!connected) {
        close(fd);
        co_return;
    }

    char buffer[64]{};
    RecvAwaitable recv_awaitable(&controller, buffer, sizeof(buffer) - 1);
    auto received = co_await recv_awaitable;
    if (!received) {
        close(fd);
        co_return;
    }

    const std::string reply = "world";
    SendAwaitable send_awaitable(&controller, reply.c_str(), reply.size());
    auto sent = co_await send_awaitable;
    if (sent && std::string(buffer, received.value()) == "hello") {
        g_client_ok.store(true, std::memory_order_release);
    }

    close(fd);
}

}  // namespace

int main() {
    const int port = 20063;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "[T63] failed to create listen socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[T63] bind failed: " << std::strerror(errno) << '\n';
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0) {
        std::cerr << "[T63] listen failed: " << std::strerror(errno) << '\n';
        close(listen_fd);
        return 1;
    }

    TestScheduler scheduler;
    scheduler.start();
    scheduler.spawn(serverTask(listen_fd));
    scheduler.spawn(clientTask("127.0.0.1", port));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    scheduler.stop();
    close(listen_fd);

    if (!g_server_ok.load(std::memory_order_acquire) ||
        !g_client_ok.load(std::memory_order_acquire)) {
        std::cerr << "[T63] expected CustomSequenceAwaitable exchange to succeed\n";
        return 1;
    }

    std::cout << "T63-CustomSequenceAwaitable PASS\n";
    return 0;
}
