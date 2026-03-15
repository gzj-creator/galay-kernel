#include "galay-kernel/kernel/Awaitable.h"
#include <cerrno>
#include <cstdint>
#include <iostream>

using namespace galay::kernel;

namespace {

Coroutine noopTask() {
    co_return;
}

bool verifyPromiseDirectAccessors() {
    Coroutine co = noopTask();
    auto task = detail::CoroutineAccess::taskRef(co);
    auto erased_handle = task.state()->m_handle;
    if (!erased_handle) {
        std::cerr << "[T39] coroutine handle is invalid\n";
        return false;
    }
    auto handle = std::coroutine_handle<Coroutine::promise_type>::from_address(
        erased_handle.address());

    const TaskRef& promise_task = handle.promise().taskRefView();
    if (!promise_task.isValid() || promise_task.state() != task.state()) {
        std::cerr << "[T39] promise taskRefView does not match coroutine task state\n";
        return false;
    }

    auto& promise_coro = handle.promise().coroutineRef();
    if (!promise_coro.isValid() || detail::CoroutineAccess::taskRef(promise_coro).state() != task.state()) {
        std::cerr << "[T39] promise coroutineRef does not match coroutine task state\n";
        return false;
    }

    erased_handle.resume();
    return true;
}

template <typename ResultT>
uint32_t systemCode(const std::expected<ResultT, IOError>& result) {
    return static_cast<uint32_t>(result.error().code() >> 32);
}

bool verifyAwaitableAddResultHelper() {
    {
        std::expected<size_t, IOError> result = static_cast<size_t>(7);
        if (detail::finalizeAwaitableAddResult(1, kSendFailed, result)) {
            std::cerr << "[T39] OK path should not suspend\n";
            return false;
        }
        if (!result || *result != 7) {
            std::cerr << "[T39] OK path should preserve successful result\n";
            return false;
        }
    }

    {
        std::expected<size_t, IOError> result = static_cast<size_t>(11);
        if (!detail::finalizeAwaitableAddResult(0, kSendFailed, result)) {
            std::cerr << "[T39] pending path should suspend\n";
            return false;
        }
        if (!result || *result != 11) {
            std::cerr << "[T39] pending path should preserve result payload\n";
            return false;
        }
    }

    {
        std::expected<size_t, IOError> result = static_cast<size_t>(0);
        if (detail::finalizeAwaitableAddResult(-ECONNRESET, kRecvFailed, result)) {
            std::cerr << "[T39] negative errno path should not suspend\n";
            return false;
        }
        if (result || !IOError::contains(result.error().code(), kRecvFailed) ||
            systemCode(result) != static_cast<uint32_t>(ECONNRESET)) {
            std::cerr << "[T39] negative errno path should map ret to system code\n";
            return false;
        }
    }

    {
        errno = EPIPE;
        std::expected<void, IOError> result{};
        if (detail::finalizeAwaitableAddResult(-1, kSendFailed, result)) {
            std::cerr << "[T39] errno fallback path should not suspend\n";
            return false;
        }
        if (result || !IOError::contains(result.error().code(), kSendFailed) ||
            systemCode(result) != static_cast<uint32_t>(EPIPE)) {
            std::cerr << "[T39] errno fallback path should use errno\n";
            return false;
        }
    }

    return true;
}

}  // namespace

int main() {
    if (!verifyPromiseDirectAccessors()) {
        return 1;
    }

    if (!verifyAwaitableAddResultHelper()) {
        return 1;
    }

    std::cout << "T39-AwaitableHotPathHelpers PASS\n";
    return 0;
}
