/**
 * @file T87-awaitable_builder_state_machine_bridge.cc
 * @brief 用途：验证 builder 状态机入口能生成可 co_await 的 Awaitable。
 * 关键覆盖点：`AwaitableBuilder<ResultT>::fromStateMachine(...).build()`。
 * 通过条件：经由 builder 构建的状态机 awaitable 成功完成并返回预期结果。
 */

#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/kernel/Task.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <expected>
#include <iostream>
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
using namespace std::chrono_literals;

namespace {

using BuilderResult = std::expected<size_t, IOError>;

struct ReadOnceMachine {
    using result_type = BuilderResult;

    MachineAction<result_type> advance() {
        if (m_result.has_value()) {
            return MachineAction<result_type>::complete(std::move(*m_result));
        }
        return MachineAction<result_type>::waitRead(m_buffer, sizeof(m_buffer));
    }

    void onRead(std::expected<size_t, IOError> result) {
        m_result = std::move(result);
    }

    void onWrite(std::expected<size_t, IOError>) {}

private:
    char m_buffer[8]{};
    std::optional<BuilderResult> m_result;
};

struct TestState {
    std::atomic<bool> done{false};
    std::atomic<bool> success{false};
};

Task<void> builderTask(TestState* state, int fd) {
    IOController controller(GHandle{.fd = fd});
    auto awaitable = AwaitableBuilder<BuilderResult>::fromStateMachine(
        &controller,
        ReadOnceMachine{}
    ).build();

    auto result = co_await awaitable;
    state->success.store(result.has_value() && result.value() == 5, std::memory_order_release);
    state->done.store(true, std::memory_order_release);
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
        std::cerr << "[T87] socketpair failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    TestScheduler scheduler;
    scheduler.start();

    TestState state;
    scheduleTask(scheduler, builderTask(&state, fds[0]));
    constexpr char payload[] = "hello";
    ::send(fds[1], payload, sizeof(payload) - 1, 0);

    const bool completed = waitUntil(state.done);
    scheduler.stop();
    close(fds[0]);
    close(fds[1]);

    if (!completed) {
        std::cerr << "[T87] builder state machine timed out\n";
        return 1;
    }
    if (!state.success.load(std::memory_order_acquire)) {
        std::cerr << "[T87] builder state machine result mismatch\n";
        return 1;
    }

    std::cout << "T87-AwaitableBuilderStateMachineBridge PASS\n";
    return 0;
}
