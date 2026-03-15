/**
 * @file Coroutine.h
 * @brief C++20 coroutine wrappers for galay-kernel
 */

#ifndef GALAY_KERNEL_COROUTINE_H
#define GALAY_KERNEL_COROUTINE_H

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace galay::kernel
{

class Coroutine;
class Scheduler;
class Waker;
class Runtime;
class PromiseType;
template <typename T>
class TaskPromise;
template <typename T>
class Task;
template <typename T>
class JoinHandle;
struct WaitResult;
struct SpawnAwaitable;
struct TaskState;
class TaskRef;

namespace detail
{

Runtime* currentRuntime() noexcept;
Runtime* swapCurrentRuntime(Runtime* runtime) noexcept;
bool scheduleTask(const TaskRef& task) noexcept;
bool scheduleTaskDeferred(const TaskRef& task) noexcept;
bool spawnCoroutine(Scheduler* scheduler, Coroutine co) noexcept;
bool spawnCoroutineImmediately(Scheduler* scheduler, Coroutine co) noexcept;
std::thread::id schedulerThreadId(Scheduler* scheduler) noexcept;
void completeTaskState(const TaskRef& task) noexcept;
struct CoroutineAccess;
struct TaskAccess;

} // namespace detail

class TaskRef
{
public:
    TaskRef() noexcept = default;
    TaskRef(const TaskRef& other) noexcept;
    TaskRef(TaskRef&& other) noexcept;
    ~TaskRef();

    TaskRef& operator=(const TaskRef& other) noexcept;
    TaskRef& operator=(TaskRef&& other) noexcept;

    bool isValid() const noexcept { return m_state != nullptr; }
    TaskState* state() const noexcept { return m_state; }
    Scheduler* belongScheduler() const noexcept;

private:
    friend class Coroutine;
    template <typename T>
    friend class Task;
    template <typename T>
    friend class TaskPromise;

    explicit TaskRef(TaskState* state, bool retainRef) noexcept;

    void retain() noexcept;
    void release() noexcept;

    TaskState* m_state = nullptr;
};

struct alignas(64) TaskState
{
    template <typename Promise>
    explicit TaskState(std::coroutine_handle<Promise> handle) noexcept
        : m_handle(handle) {}

    std::coroutine_handle<> m_handle = nullptr;
    Scheduler* m_scheduler = nullptr;
    Runtime* m_runtime = nullptr;
    std::optional<TaskRef> m_then;
    std::optional<TaskRef> m_next;
    std::atomic<uint32_t> m_refs{1};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_queued{false};
};

namespace detail
{

inline Runtime* taskRuntime(const TaskRef& task) noexcept
{
    auto* state = task.state();
    return state ? state->m_runtime : nullptr;
}

inline void setTaskRuntime(const TaskRef& task, Runtime* runtime) noexcept
{
    if (auto* state = task.state()) {
        state->m_runtime = runtime;
        if (state->m_then.has_value()) {
            if (auto* then_state = state->m_then->state(); then_state && then_state->m_runtime == nullptr) {
                then_state->m_runtime = runtime;
            }
        }
    }
}

inline void inheritTaskRuntime(const TaskRef& task, Runtime* runtime) noexcept
{
    if (auto* state = task.state(); state && state->m_runtime == nullptr) {
        state->m_runtime = runtime;
    }
}

inline void setTaskScheduler(const TaskRef& task, Scheduler* scheduler) noexcept
{
    if (auto* state = task.state()) {
        state->m_scheduler = scheduler;
        if (state->m_then.has_value() && state->m_then->belongScheduler() == nullptr) {
            setTaskScheduler(*state->m_then, scheduler);
        }
    }
}

class CurrentRuntimeScope
{
public:
    explicit CurrentRuntimeScope(Runtime* runtime) noexcept
        : m_previous(swapCurrentRuntime(runtime)) {}

    ~CurrentRuntimeScope()
    {
        swapCurrentRuntime(m_previous);
    }

    CurrentRuntimeScope(const CurrentRuntimeScope&) = delete;
    CurrentRuntimeScope& operator=(const CurrentRuntimeScope&) = delete;

private:
    Runtime* m_previous;
};

} // namespace detail

template <typename T>
struct TaskCompletionState
{
    static_assert(!std::is_reference_v<T>, "Task<T> does not support reference results");

    template <typename U>
    void setValue(U&& value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_value = std::forward<U>(value);
            m_ready = true;
        }
        m_cv.notify_all();
    }

    void setException(std::exception_ptr exception)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exception = std::move(exception);
            m_ready = true;
        }
        m_cv.notify_all();
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_ready; });
    }

    T take()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_ready; });
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
        if (m_consumed) {
            throw std::runtime_error("task result already consumed");
        }
        m_consumed = true;
        return std::move(*m_value);
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::optional<T> m_value;
    std::exception_ptr m_exception;
    bool m_ready = false;
    bool m_consumed = false;
};

template <>
struct TaskCompletionState<void>
{
    void setValue()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ready = true;
        }
        m_cv.notify_all();
    }

    void setException(std::exception_ptr exception)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exception = std::move(exception);
            m_ready = true;
        }
        m_cv.notify_all();
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_ready; });
    }

    void take()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_ready; });
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
        if (m_consumed) {
            throw std::runtime_error("task result already consumed");
        }
        m_consumed = true;
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::exception_ptr m_exception;
    bool m_ready = false;
    bool m_consumed = false;
};

class Coroutine
{
    friend struct WaitResult;

public:
    using promise_type = PromiseType;

    friend class PromiseType;
    friend class Waker;
    friend class Scheduler;
    template <typename T>
    friend class Task;

    template <typename T>
    friend class MpscChannel;
    friend struct detail::CoroutineAccess;

    Coroutine() noexcept = default;
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine(const Coroutine& other) noexcept;

    Coroutine& operator=(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other) noexcept;

    bool isValid() const { return m_task.isValid(); }
    bool done() const;
    Coroutine& then(Coroutine co) &;
    Coroutine&& then(Coroutine co) &&;
    WaitResult wait();

private:
    friend class Waker;

    explicit Coroutine(TaskRef task) noexcept;

    TaskRef m_task;
};

namespace detail
{

struct CoroutineAccess
{
    static Scheduler* belongScheduler(const Coroutine& co)
    {
        auto* state = co.m_task.state();
        return state ? state->m_scheduler : nullptr;
    }

    static void setScheduler(Coroutine& co, Scheduler* scheduler)
    {
        auto* state = co.m_task.state();
        if (!state) {
            return;
        }
        state->m_scheduler = scheduler;
        if (state->m_then.has_value() && state->m_then->belongScheduler() == nullptr) {
            detail::setTaskScheduler(*state->m_then, scheduler);
        }
    }

    static TaskRef taskRef(const Coroutine& co) noexcept
    {
        return co.m_task;
    }

    static TaskRef detachTask(Coroutine&& co) noexcept
    {
        return std::move(co.m_task);
    }

    static std::thread::id threadId(const Coroutine& co)
    {
        return detail::schedulerThreadId(belongScheduler(co));
    }

    static void resume(Coroutine& co)
    {
        auto* state = co.m_task.state();
        if (state && state->m_handle && state->m_scheduler &&
            !state->m_done.load(std::memory_order_relaxed)) {
            if (!state->m_queued.exchange(true, std::memory_order_acq_rel)) {
                detail::scheduleTask(co.m_task);
            }
        }
    }
};

} // namespace detail

template <typename T>
class Task
{
public:
    using promise_type = TaskPromise<T>;

    Task() noexcept = default;
    Task(Task&& other) noexcept = default;
    Task& operator=(Task&& other) noexcept = default;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool isValid() const { return m_task.isValid() && static_cast<bool>(m_completion); }
    bool done() const
    {
        auto* state = m_task.state();
        return !state || state->m_done.load(std::memory_order_acquire);
    }

private:
    friend class Runtime;
    template <typename U>
    friend class JoinHandle;
    template <typename U>
    friend class TaskPromise;
    friend struct detail::TaskAccess;

    explicit Task(TaskRef task, std::shared_ptr<TaskCompletionState<T>> completion) noexcept
        : m_task(std::move(task))
        , m_completion(std::move(completion))
    {
    }

    T takeResult()
    {
        return m_completion->take();
    }

    const std::shared_ptr<TaskCompletionState<T>>& completionState() const noexcept
    {
        return m_completion;
    }

    TaskRef m_task;
    std::shared_ptr<TaskCompletionState<T>> m_completion;
};

template <>
class Task<void>
{
public:
    using promise_type = TaskPromise<void>;

    Task() noexcept = default;
    Task(Task&& other) noexcept = default;
    Task& operator=(Task&& other) noexcept = default;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool isValid() const { return m_task.isValid() && static_cast<bool>(m_completion); }
    bool done() const
    {
        auto* state = m_task.state();
        return !state || state->m_done.load(std::memory_order_acquire);
    }

private:
    friend class Runtime;
    template <typename U>
    friend class JoinHandle;
    friend class TaskPromise<void>;
    friend struct detail::TaskAccess;

    explicit Task(TaskRef task, std::shared_ptr<TaskCompletionState<void>> completion) noexcept
        : m_task(std::move(task))
        , m_completion(std::move(completion))
    {
    }

    void takeResult()
    {
        m_completion->take();
    }

    const std::shared_ptr<TaskCompletionState<void>>& completionState() const noexcept
    {
        return m_completion;
    }

    TaskRef m_task;
    std::shared_ptr<TaskCompletionState<void>> m_completion;
};

template <typename T>
class JoinHandle
{
public:
    JoinHandle() noexcept = default;
    explicit JoinHandle(std::shared_ptr<TaskCompletionState<T>> completion) noexcept
        : m_completion(std::move(completion))
    {
    }

    JoinHandle(JoinHandle&& other) noexcept = default;
    JoinHandle& operator=(JoinHandle&& other) noexcept = default;

    JoinHandle(const JoinHandle&) = delete;
    JoinHandle& operator=(const JoinHandle&) = delete;

    bool isValid() const noexcept { return static_cast<bool>(m_completion); }

    void wait() const
    {
        if (!m_completion) {
            throw std::runtime_error("invalid join handle");
        }
        m_completion->wait();
    }

    T join()
    {
        if (!m_completion) {
            throw std::runtime_error("invalid join handle");
        }
        return m_completion->take();
    }

private:
    std::shared_ptr<TaskCompletionState<T>> m_completion;
};

template <>
class JoinHandle<void>
{
public:
    JoinHandle() noexcept = default;
    explicit JoinHandle(std::shared_ptr<TaskCompletionState<void>> completion) noexcept
        : m_completion(std::move(completion))
    {
    }

    JoinHandle(JoinHandle&& other) noexcept = default;
    JoinHandle& operator=(JoinHandle&& other) noexcept = default;

    JoinHandle(const JoinHandle&) = delete;
    JoinHandle& operator=(const JoinHandle&) = delete;

    bool isValid() const noexcept { return static_cast<bool>(m_completion); }

    void wait() const
    {
        if (!m_completion) {
            throw std::runtime_error("invalid join handle");
        }
        m_completion->wait();
    }

    void join()
    {
        if (!m_completion) {
            throw std::runtime_error("invalid join handle");
        }
        m_completion->take();
    }

private:
    std::shared_ptr<TaskCompletionState<void>> m_completion;
};

namespace detail
{

struct TaskAccess
{
    template <typename T>
    static const TaskRef& taskRef(const Task<T>& task) noexcept
    {
        return task.m_task;
    }

    template <typename T>
    static auto completionState(const Task<T>& task) noexcept -> const std::shared_ptr<TaskCompletionState<T>>&
    {
        return task.m_completion;
    }

    template <typename T>
    static decltype(auto) takeResult(Task<T>& task)
    {
        return task.takeResult();
    }

    template <typename T>
    static Coroutine asCoroutine(const Task<T>& task) noexcept
    {
        return Coroutine(task.m_task);
    }
};

} // namespace detail

struct WaitResult
{
public:
    explicit WaitResult(Coroutine co)
        : m_co(std::move(co))
    {
    }

    bool await_ready()
    {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle)
    {
        TaskRef waitingTask = handle.promise().taskRefView();
        detail::inheritTaskRuntime(m_co.m_task, detail::taskRuntime(waitingTask));
        auto* scheduler = waitingTask.belongScheduler();
        if (!detail::spawnCoroutineImmediately(scheduler, m_co)) {
            return false;
        }
        if (m_co.done()) {
            return false;
        }
        m_co.m_task.state()->m_next = std::move(waitingTask);
        return true;
    }

    void await_resume() {}

private:
    Coroutine m_co;
};

struct SpawnAwaitable
{
public:
    explicit SpawnAwaitable(Coroutine co)
        : m_co(std::move(co))
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        TaskRef currentTask = handle.promise().taskRefView();
        detail::inheritTaskRuntime(detail::CoroutineAccess::taskRef(m_co), detail::taskRuntime(currentTask));
        detail::spawnCoroutine(currentTask.belongScheduler(), m_co);
        return false;
    }

    void await_resume() noexcept {}

private:
    Coroutine m_co;
};

inline SpawnAwaitable spawn(Coroutine co)
{
    return SpawnAwaitable(std::move(co));
}

template <typename T>
inline SpawnAwaitable spawn(Task<T> task)
{
    return SpawnAwaitable(detail::TaskAccess::asCoroutine(task));
}

class PromiseType
{
public:
    using ReSchedulerType = bool;

    int get_return_object_on_alloaction_failure() noexcept { return -1; }
    Coroutine get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always yield_value(ReSchedulerType flag) noexcept;
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept;
    void return_void() noexcept;

    Coroutine getCoroutine() { return m_coroutine; }
    Coroutine& coroutineRef() noexcept { return m_coroutine; }
    const Coroutine& coroutineRef() const noexcept { return m_coroutine; }
    const TaskRef& taskRefView() const noexcept { return m_coroutine.m_task; }

private:
    Coroutine m_coroutine;
};

template <typename T>
class TaskPromise
{
public:
    using ReSchedulerType = bool;

    int get_return_object_on_alloaction_failure() noexcept { return -1; }

    Task<T> get_return_object() noexcept
    {
        auto handle = std::coroutine_handle<TaskPromise<T>>::from_promise(*this);
        m_task = TaskRef(new TaskState(handle), false);
        detail::inheritTaskRuntime(m_task, detail::currentRuntime());
        return Task<T>(m_task, m_completion);
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    std::suspend_always yield_value(ReSchedulerType flag) noexcept
    {
        if (flag) {
            detail::scheduleTaskDeferred(m_task);
        }
        return {};
    }

    std::suspend_never final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept
    {
        m_completion->setException(std::current_exception());
        detail::completeTaskState(m_task);
    }

    template <typename U>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
    {
        m_completion->setValue(std::forward<U>(value));
        detail::completeTaskState(m_task);
    }

    const TaskRef& taskRefView() const noexcept { return m_task; }

private:
    TaskRef m_task;
    std::shared_ptr<TaskCompletionState<T>> m_completion = std::make_shared<TaskCompletionState<T>>();
};

template <>
class TaskPromise<void>
{
public:
    using ReSchedulerType = bool;

    int get_return_object_on_alloaction_failure() noexcept { return -1; }

    Task<void> get_return_object() noexcept
    {
        auto handle = std::coroutine_handle<TaskPromise<void>>::from_promise(*this);
        m_task = TaskRef(new TaskState(handle), false);
        detail::inheritTaskRuntime(m_task, detail::currentRuntime());
        return Task<void>(m_task, m_completion);
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    std::suspend_always yield_value(ReSchedulerType flag) noexcept
    {
        if (flag) {
            detail::scheduleTaskDeferred(m_task);
        }
        return {};
    }

    std::suspend_never final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept
    {
        m_completion->setException(std::current_exception());
        detail::completeTaskState(m_task);
    }

    void return_void() noexcept
    {
        m_completion->setValue();
        detail::completeTaskState(m_task);
    }

    const TaskRef& taskRefView() const noexcept { return m_task; }

private:
    TaskRef m_task;
    std::shared_ptr<TaskCompletionState<void>> m_completion = std::make_shared<TaskCompletionState<void>>();
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_COROUTINE_H
