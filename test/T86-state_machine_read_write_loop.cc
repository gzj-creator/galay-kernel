/**
 * @file T86-state_machine_read_write_loop.cc
 * @brief 用途：验证状态机 Awaitable 能完成 read -> write -> read 的事件切换。
 * 关键覆盖点：`StateMachineAwaitable`、`WaitRead`、`WaitWrite`、完成后恢复协程。
 * 通过条件：状态机完成完整收发环并返回预期结果。
 */

#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/kernel/Task.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <expected>
#include <iostream>
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
using namespace std::chrono_literals;

namespace {

using MachineResult = std::expected<std::string, IOError>;

struct ReadWriteLoopMachine {
    using result_type = MachineResult;

    MachineAction<result_type> advance() {
        if (m_result.has_value()) {
            return MachineAction<result_type>::complete(std::move(*m_result));
        }

        switch (m_phase) {
        case Phase::ReadHello:
            return MachineAction<result_type>::waitRead(m_hello.data(), m_hello.size());
        case Phase::WritePong:
            return MachineAction<result_type>::waitWrite(m_pong.data(), m_pong.size());
        case Phase::ReadWorld:
            return MachineAction<result_type>::waitRead(m_world.data(), m_world.size());
        case Phase::Done:
            return MachineAction<result_type>::fail(IOError(kParamInvalid, 0));
        }
        return MachineAction<result_type>::fail(IOError(kParamInvalid, 0));
    }

    void onRead(std::expected<size_t, IOError> result) {
        if (!result) {
            m_result = std::unexpected(result.error());
            m_phase = Phase::Done;
            return;
        }

        if (m_phase == Phase::ReadHello) {
            if (result.value() != m_hello.size() ||
                std::string(m_hello.data(), result.value()) != "hello") {
                m_result = std::unexpected(IOError(kReadFailed, 0));
            } else {
                m_phase = Phase::WritePong;
            }
            if (m_result.has_value()) {
                m_phase = Phase::Done;
            }
            return;
        }

        if (m_phase == Phase::ReadWorld) {
            if (result.value() != m_world.size()) {
                m_result = std::unexpected(IOError(kReadFailed, 0));
            } else {
                m_result = std::string(m_world.data(), result.value());
            }
            m_phase = Phase::Done;
            return;
        }

        m_result = std::unexpected(IOError(kParamInvalid, 0));
        m_phase = Phase::Done;
    }

    void onWrite(std::expected<size_t, IOError> result) {
        if (!result) {
            m_result = std::unexpected(result.error());
            m_phase = Phase::Done;
            return;
        }

        if (m_phase != Phase::WritePong || result.value() != m_pong.size()) {
            m_result = std::unexpected(IOError(kSendFailed, 0));
            m_phase = Phase::Done;
            return;
        }

        m_phase = Phase::ReadWorld;
    }

private:
    enum class Phase {
        ReadHello,
        WritePong,
        ReadWorld,
        Done,
    };

    Phase m_phase = Phase::ReadHello;
    std::optional<MachineResult> m_result;
    std::array<char, 5> m_hello{};
    std::array<char, 4> m_pong{'p', 'o', 'n', 'g'};
    std::array<char, 5> m_world{};
};

struct TestState {
    std::atomic<bool> done{false};
    std::atomic<bool> success{false};
};

Task<void> machineTask(TestState* state, int fd) {
    IOController controller(GHandle{.fd = fd});
    StateMachineAwaitable<ReadWriteLoopMachine> awaitable(&controller, ReadWriteLoopMachine{});

    auto result = co_await awaitable;
    state->success.store(result.has_value() && result.value() == "world", std::memory_order_release);
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
        std::cerr << "[T86] socketpair failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    TestScheduler scheduler;
    scheduler.start();

    TestState state;
    std::thread peer([&]() {
        constexpr char hello[] = "hello";
        constexpr char world[] = "world";
        char pong[4]{};

        ::send(fds[1], hello, sizeof(hello) - 1, 0);
        const ssize_t pong_n = ::recv(fds[1], pong, sizeof(pong), 0);
        if (pong_n == static_cast<ssize_t>(sizeof(pong)) &&
            std::string(pong, sizeof(pong)) == "pong") {
            ::send(fds[1], world, sizeof(world) - 1, 0);
        }
    });

    scheduleTask(scheduler, machineTask(&state, fds[0]));

    const bool completed = waitUntil(state.done);
    scheduler.stop();
    peer.join();
    close(fds[0]);
    close(fds[1]);

    if (!completed) {
        std::cerr << "[T86] state machine timed out\n";
        return 1;
    }
    if (!state.success.load(std::memory_order_acquire)) {
        std::cerr << "[T86] state machine result mismatch\n";
        return 1;
    }

    std::cout << "T86-StateMachineReadWriteLoop PASS\n";
    return 0;
}
