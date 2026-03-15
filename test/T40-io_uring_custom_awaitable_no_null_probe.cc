/**
 * @file T40-io_uring_custom_awaitable_no_null_probe.cc
 * @brief 用途：验证 io_uring 自定义 Awaitable 不依赖空指针探测也能完成注册。
 * 关键覆盖点：自定义 Awaitable 注册、io_uring 路径接线、无空探测完成恢复。
 * 通过条件：io_uring 自定义 Awaitable 路径可用且测试返回 0。
 */

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Awaitable.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace galay::kernel;
using namespace std::chrono_literals;

namespace {

struct ProbeSendContext : public SendIOContext {
    using SendIOContext::SendIOContext;

    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override {
        if (cqe == nullptr) {
            ++null_probes;
            return false;
        }
        return SendIOContext::handleComplete(cqe, handle);
    }

    int null_probes = 0;
};

struct ProbeSendAwaitable : public CustomAwaitable {
    ProbeSendAwaitable(IOController* controller, const char* buffer, size_t length)
        : CustomAwaitable(controller)
        , m_send(buffer, length) {
        addTask(SEND, &m_send);
    }

    bool await_ready() { return false; }

    std::expected<size_t, IOError> await_resume() {
        onCompleted();
        return std::move(m_send.m_result);
    }

    ProbeSendContext m_send;
};

struct TestState {
    std::atomic<bool> done{false};
    std::atomic<bool> success{false};
    std::atomic<int> null_probes{0};
};

Coroutine sendCoroutine(TestState* state, int fd, const char* msg, size_t len) {
    IOController controller(GHandle{.fd = fd});
    ProbeSendAwaitable awaitable(&controller, msg, len);
    auto result = co_await awaitable;

    state->null_probes.store(awaitable.m_send.null_probes, std::memory_order_release);
    state->success.store(result.has_value() && result.value() == len, std::memory_order_release);
    state->done.store(true, std::memory_order_release);
    co_return;
}

bool waitUntil(const std::atomic<bool>& flag,
               std::chrono::milliseconds timeout = 1000ms,
               std::chrono::milliseconds step = 2ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(step);
    }
    return flag.load(std::memory_order_acquire);
}

}  // namespace

int main() {
    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        std::cerr << "[T40] socketpair failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    for (int fd : fds) {
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    const char payload[] = "hello-iouring-custom";
    char recv_buf[64]{};
    std::atomic<bool> peer_done{false};
    std::thread peer([&]() {
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline) {
            const ssize_t n = recv(fds[1], recv_buf, sizeof(recv_buf), 0);
            if (n == static_cast<ssize_t>(sizeof(payload) - 1)) {
                peer_done.store(true, std::memory_order_release);
                return;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            break;
        }
        peer_done.store(false, std::memory_order_release);
    });

    TestState state;
    IOUringScheduler scheduler;
    scheduler.start();
    scheduler.spawn(sendCoroutine(&state, fds[0], payload, sizeof(payload) - 1));

    const bool coroutine_done = waitUntil(state.done);
    scheduler.stop();
    peer.join();

    close(fds[0]);
    close(fds[1]);

    if (!coroutine_done) {
        std::cerr << "[T40] coroutine timed out\n";
        return 1;
    }
    if (!state.success.load(std::memory_order_acquire)) {
        std::cerr << "[T40] send coroutine failed\n";
        return 1;
    }
    if (!peer_done.load(std::memory_order_acquire)) {
        std::cerr << "[T40] peer recv failed\n";
        return 1;
    }
    if (std::strcmp(recv_buf, payload) != 0) {
        std::cerr << "[T40] payload mismatch: " << recv_buf << "\n";
        return 1;
    }
    if (state.null_probes.load(std::memory_order_acquire) != 0) {
        std::cerr << "[T40] expected no null cqe probes, got "
                  << state.null_probes.load(std::memory_order_acquire) << "\n";
        return 1;
    }

    std::cout << "T40-IOUringCustomAwaitableNoNullProbe PASS\n";
    return 0;
}

#else

int main() {
    std::cout << "T40-IOUringCustomAwaitableNoNullProbe SKIP\n";
    return 0;
}

#endif
