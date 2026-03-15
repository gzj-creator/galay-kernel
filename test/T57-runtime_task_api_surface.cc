/**
 * @file T57-runtime_task_api_surface.cc
 * @brief 用途：验证 `Runtime`、`Task` 与 `JoinHandle` 的公开 API 形态保持完整。
 * 关键覆盖点：编译期概念检查、公开成员存在性、关键运行时 API 语义。
 * 通过条件：编译期与运行期检查全部通过，测试返回 0。
 */

#include "galay-kernel/kernel/Runtime.h"

#include <concepts>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

using namespace galay::kernel;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

Task<int> simpleTask(int value) {
    co_return value;
}

Task<void> verifyCurrentRuntimeHandle() {
    auto current = RuntimeHandle::tryCurrent();
    require(current.has_value(), "RuntimeHandle::tryCurrent should succeed inside runtime context");

    auto nested = RuntimeHandle::current().spawn(simpleTask(19));
    require(nested.join() == 19, "RuntimeHandle::current should return a working handle");
    co_return;
}

template <typename T>
concept HasJoinHandleResult = requires(JoinHandle<T> handle) {
    handle.result();
};

template <typename T>
concept HasTaskRef = requires(Task<T> task) {
    task.taskRef();
};

template <typename T>
concept HasBelongSchedulerGetter = requires(Task<T> task) {
    task.belongScheduler();
};

template <typename T>
concept HasBelongSchedulerSetter = requires(Task<T> task, Scheduler* scheduler) {
    task.belongScheduler(scheduler);
};

template <typename T>
concept HasThreadId = requires(Task<T> task) {
    task.threadId();
};

template <typename T>
concept HasAsCoroutine = requires(Task<T> task) {
    task.asCoroutine();
};

template <typename T>
concept HasJoinHandleWait = requires(JoinHandle<T> handle) {
    handle.wait();
};

template <typename = void>
concept HasRuntimeHandleCurrent = requires {
    { RuntimeHandle::current() } -> std::same_as<RuntimeHandle>;
};

template <typename = void>
concept HasRuntimeHandleTryCurrent = requires {
    { RuntimeHandle::tryCurrent() } -> std::same_as<std::optional<RuntimeHandle>>;
};

struct BlockingCallable {
    int operator()() const { return 23; }
};

template <typename R>
concept HasRuntimeSpawnBlocking = requires(R runtime) {
    runtime.spawnBlocking(BlockingCallable{});
};

template <typename C>
concept HasCoroutineThenLvalue = requires(C left, C right) {
    { left.then(std::move(right)) } -> std::same_as<Coroutine&>;
};

template <typename C>
concept HasCoroutineThenRvalue = requires(C left, C right) {
    { std::move(left).then(std::move(right)) } -> std::same_as<Coroutine&&>;
};

template <typename C>
concept HasCoroutineTaskRef = requires(C co) {
    co.taskRef();
};

template <typename C>
concept HasCoroutineDetachTask = requires(C co) {
    std::move(co).detachTask();
};

template <typename C>
concept HasCoroutineBelongSchedulerGetter = requires(C co) {
    co.belongScheduler();
};

template <typename C>
concept HasCoroutineBelongSchedulerSetter = requires(C co, Scheduler* scheduler) {
    co.belongScheduler(scheduler);
};

template <typename C>
concept HasCoroutineThreadId = requires(C co) {
    co.threadId();
};

template <typename C>
concept HasCoroutineResume = requires(C co) {
    co.resume();
};

}  // namespace

static_assert(!HasJoinHandleResult<int>);
static_assert(!HasTaskRef<int>);
static_assert(!HasBelongSchedulerGetter<int>);
static_assert(!HasBelongSchedulerSetter<int>);
static_assert(!HasThreadId<int>);
static_assert(!HasAsCoroutine<int>);
static_assert(HasJoinHandleWait<int>);
static_assert(HasRuntimeHandleCurrent<>);
static_assert(HasRuntimeHandleTryCurrent<>);
static_assert(HasRuntimeSpawnBlocking<Runtime>);
static_assert(HasCoroutineThenLvalue<Coroutine>);
static_assert(HasCoroutineThenRvalue<Coroutine>);
static_assert(!HasCoroutineTaskRef<Coroutine>);
static_assert(!HasCoroutineDetachTask<Coroutine>);
static_assert(!HasCoroutineBelongSchedulerGetter<Coroutine>);
static_assert(!HasCoroutineBelongSchedulerSetter<Coroutine>);
static_assert(!HasCoroutineThreadId<Coroutine>);
static_assert(!HasCoroutineResume<Coroutine>);

int main() {
    Runtime runtime;

    require(!RuntimeHandle::tryCurrent().has_value(), "RuntimeHandle::tryCurrent should be empty outside runtime context");
    require(runtime.blockOn(simpleTask(7)) == 7, "Runtime::blockOn should return task result");

    auto joinHandle = runtime.spawn(simpleTask(11));
    joinHandle.wait();
    require(joinHandle.join() == 11, "Runtime::spawn should return joinable handle");

    auto handle = runtime.handle();
    auto handleJoin = handle.spawn(simpleTask(13));
    handleJoin.wait();
    require(handleJoin.join() == 13, "RuntimeHandle::spawn should submit task");

    auto runtimeBlockingJoin = runtime.spawnBlocking([]() { return 17; });
    runtimeBlockingJoin.wait();
    require(runtimeBlockingJoin.join() == 17, "Runtime::spawnBlocking should return task result");

    auto blockingJoin = handle.spawnBlocking([]() { return 17; });
    blockingJoin.wait();
    require(blockingJoin.join() == 17, "RuntimeHandle::spawnBlocking should return task result");
    runtime.blockOn(verifyCurrentRuntimeHandle());

    return 0;
}
