#ifndef GALAY_KERNEL_RUNTIME_H
#define GALAY_KERNEL_RUNTIME_H

#include "BlockingExecutor.h"
#include "Task.h"
#include "ComputeScheduler.h"
#include "IOScheduler.hpp"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace galay::kernel
{

#define GALAY_RUNTIME_SCHEDULER_COUNT_AUTO static_cast<size_t>(-1)

/**
 * @brief Runtime 的绑核配置。
 *
 * @note
 * - `Mode::None` 表示不主动绑核
 * - `Mode::Sequential` 按 0..N-1 顺序分配 CPU
 * - `Mode::Custom` 要求调用方提供与 scheduler 数量完全一致的 CPU 列表
 */
struct RuntimeAffinityConfig {
    enum class Mode { None, Sequential, Custom } mode = Mode::None;
    size_t seq_io_count = 0;
    size_t seq_compute_count = 0;
    std::vector<uint32_t> custom_io_cpus;
    std::vector<uint32_t> custom_compute_cpus;
};

/**
 * @brief Runtime 的构建配置。
 *
 * 当 `*_scheduler_count` 为 `GALAY_RUNTIME_SCHEDULER_COUNT_AUTO` 时，
 * Runtime 会在首次启动时按当前机器 CPU 数自动创建默认调度器。
 */
struct RuntimeConfig {
    size_t io_scheduler_count = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO;
    size_t compute_scheduler_count = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO;
    RuntimeAffinityConfig affinity;
};

class RuntimeHandle;

/**
 * @brief 运行时入口，负责管理 IO / compute scheduler 与阻塞线程池。
 *
 * `Runtime` 可以显式注入 scheduler，也可以在首次提交任务时按配置自动创建默认
 * scheduler。实例本身不可拷贝；生命周期结束时会调用 `stop()` 停止其管理的调度器。
 */
class Runtime
{
public:
    explicit Runtime(const RuntimeConfig& config = RuntimeConfig{});
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * @brief 在 runtime 启动前注册一个 IO scheduler。
     * @return 启动前返回 `true`；若 runtime 已运行则返回 `false`
     */
    bool addIOScheduler(std::unique_ptr<IOScheduler> scheduler);

    /**
     * @brief 在 runtime 启动前注册一个 compute scheduler。
     * @return 启动前返回 `true`；若 runtime 已运行则返回 `false`
     */
    bool addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler);

    /**
     * @brief 启动 runtime 及其管理的 scheduler。
     *
     * 若未显式注册 scheduler，会按 `RuntimeConfig` 自动创建默认实例。
     * 重复调用安全，已运行时直接返回。
     */
    void start();

    /**
     * @brief 停止 runtime 及其管理的 scheduler。
     *
     * 停止顺序为 compute -> IO -> timer；重复调用安全。
     */
    void stop();

    /**
     * @brief 在 runtime 上提交一个根任务并同步等待结果。
     * @param task 要提交的任务；所有权转移到 runtime
     * @return 任务返回值；`Task<void>` 时无返回
     *
     * @throws std::runtime_error 当 runtime 无可用 scheduler 或任务提交失败
     * @throws 任务内部抛出的异常会在取结果时重新抛出
     *
     * @note 若 runtime 尚未启动，会在内部自动启动
     */
    template <typename T>
    auto blockOn(Task<T> task) -> T
    {
        Scheduler* scheduler = acquireDefaultScheduler();
        if (scheduler == nullptr) {
            throw std::runtime_error("runtime has no scheduler available for blockOn");
        }

        const TaskRef& taskRef = detail::TaskAccess::taskRef(task);
        bindTaskToRuntime(taskRef, scheduler);
        if (!submitTask(taskRef)) {
            throw std::runtime_error("failed to submit root task to runtime");
        }

        return detail::TaskAccess::takeResult(task);
    }

    /**
     * @brief 异步提交一个任务并返回可 `join()` 的句柄。
     * @param task 要提交的任务；所有权转移到 runtime
     * @return 与任务结果绑定的 `JoinHandle<T>`
     *
     * @throws std::runtime_error 当 runtime 无可用 scheduler 或任务提交失败
     *
     * @note 若 runtime 尚未启动，会在内部自动启动
     */
    template <typename T>
    JoinHandle<T> spawn(Task<T> task)
    {
        Scheduler* scheduler = acquireDefaultScheduler();
        if (scheduler == nullptr) {
            throw std::runtime_error("runtime has no scheduler available for spawn");
        }

        auto completion = detail::TaskAccess::completionState(task);
        const TaskRef& taskRef = detail::TaskAccess::taskRef(task);
        bindTaskToRuntime(taskRef, scheduler);
        if (!submitTask(taskRef)) {
            throw std::runtime_error("failed to submit task to runtime");
        }
        return JoinHandle<T>(std::move(completion));
    }

    /**
     * @brief 在线程池上执行一个阻塞 callable，并返回 join handle。
     * @param func 可调用对象；会被 move/copy 进阻塞线程池
     * @return `JoinHandle<Result>`
     *
     * @note
     * - 适合文件阻塞 IO、第三方同步库调用等不可协程化路径
     * - callable 内部会继承当前 runtime 上下文，因此可安全调用 `RuntimeHandle::tryCurrent()`
     * - 结果或异常会被捕获并在 `join()` 时交付
     */
    template <typename F>
    auto spawnBlocking(F&& func) -> JoinHandle<std::invoke_result_t<std::decay_t<F>&>>
    {
        using Fn = std::decay_t<F>;
        using Result = std::invoke_result_t<Fn&>;

        auto completion = std::make_shared<TaskCompletionState<Result>>();
        m_blockingExecutor.submit([runtime = this, completion, function = Fn(std::forward<F>(func))]() mutable {
            detail::CurrentRuntimeScope runtimeScope(runtime);
            try {
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(function);
                    completion->setValue();
                } else {
                    completion->setValue(std::invoke(function));
                }
            } catch (...) {
                completion->setException(std::current_exception());
            }
        });

        return JoinHandle<Result>(std::move(completion));
    }

    /**
     * @brief 获取一个轻量 `RuntimeHandle`，用于把当前 runtime 传递到其他层。
     */
    RuntimeHandle handle() noexcept;

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    size_t getIOSchedulerCount() const { return m_io_schedulers.size(); }
    size_t getComputeSchedulerCount() const { return m_compute_schedulers.size(); }

    IOScheduler* getIOScheduler(size_t index);
    ComputeScheduler* getComputeScheduler(size_t index);
    IOScheduler* getNextIOScheduler();
    ComputeScheduler* getNextComputeScheduler();

private:
    void createDefaultSchedulers();
    void applyAffinityConfig();
    void ensureStarted();
    Scheduler* acquireDefaultScheduler();
    void bindTaskToRuntime(const TaskRef& task, Scheduler* scheduler);
    bool submitTask(const TaskRef& task);
    static size_t getCPUCount();

    std::vector<std::unique_ptr<IOScheduler>> m_io_schedulers;
    std::vector<std::unique_ptr<ComputeScheduler>> m_compute_schedulers;

    std::atomic<uint32_t> m_io_index{0};
    std::atomic<uint32_t> m_compute_index{0};

    BlockingExecutor m_blockingExecutor;
    RuntimeConfig m_config;
    std::atomic<bool> m_running{false};
};

class RuntimeHandle
{
public:
    RuntimeHandle() noexcept = default;
    explicit RuntimeHandle(Runtime* runtime) noexcept
        : m_runtime(runtime)
    {
    }

    /**
     * @brief 获取当前线程绑定的 runtime handle。
     * @throws std::runtime_error 若当前执行路径不在 runtime 上下文中
     */
    static RuntimeHandle current();

    /**
     * @brief 尝试获取当前线程绑定的 runtime handle。
     * @return 当前 runtime 存在时返回值，否则返回 `std::nullopt`
     */
    static std::optional<RuntimeHandle> tryCurrent();

    bool isValid() const noexcept { return m_runtime != nullptr; }

    template <typename T>
    JoinHandle<T> spawn(Task<T> task) const
    {
        return requireRuntime()->spawn(std::move(task));
    }

    template <typename F>
    auto spawnBlocking(F&& func) const -> JoinHandle<std::invoke_result_t<std::decay_t<F>&>>
    {
        return requireRuntime()->spawnBlocking(std::forward<F>(func));
    }

private:
    Runtime* requireRuntime() const
    {
        if (m_runtime == nullptr) {
            throw std::runtime_error("runtime handle is not bound to a runtime");
        }
        return m_runtime;
    }

    Runtime* m_runtime = nullptr;
};

class RuntimeBuilder
{
public:
    /**
     * @brief 设置 IO scheduler 数量。
     * @note 传 `GALAY_RUNTIME_SCHEDULER_COUNT_AUTO` 时由 runtime 按 CPU 数自动推导
     */
    RuntimeBuilder& ioSchedulerCount(size_t n)
    {
        m_config.io_scheduler_count = n;
        return *this;
    }

    /**
     * @brief 设置 compute scheduler 数量。
     * @note 传 `GALAY_RUNTIME_SCHEDULER_COUNT_AUTO` 时由 runtime 按 CPU 数自动推导
     */
    RuntimeBuilder& computeSchedulerCount(size_t n)
    {
        m_config.compute_scheduler_count = n;
        return *this;
    }

    /**
     * @brief 对前 `ioCount` / `computeCount` 个 scheduler 依次分配 CPU 亲和性。
     */
    RuntimeBuilder& sequentialAffinity(size_t ioCount, size_t computeCount)
    {
        m_config.affinity.mode = RuntimeAffinityConfig::Mode::Sequential;
        m_config.affinity.seq_io_count = ioCount;
        m_config.affinity.seq_compute_count = computeCount;
        return *this;
    }

    /**
     * @brief 为每个 scheduler 指定显式 CPU 亲和性列表。
     * @return 列表长度与当前 scheduler 配置完全匹配时返回 `true`
     */
    bool customAffinity(std::vector<uint32_t> ioCpus, std::vector<uint32_t> computeCpus)
    {
        if (ioCpus.size() != m_config.io_scheduler_count ||
            computeCpus.size() != m_config.compute_scheduler_count) {
            return false;
        }
        m_config.affinity.mode = RuntimeAffinityConfig::Mode::Custom;
        m_config.affinity.custom_io_cpus = std::move(ioCpus);
        m_config.affinity.custom_compute_cpus = std::move(computeCpus);
        return true;
    }

    /**
     * @brief 直接覆盖完整 affinity 配置。
     */
    RuntimeBuilder& applyAffinity(const RuntimeAffinityConfig& affinity)
    {
        m_config.affinity = affinity;
        return *this;
    }

    /**
     * @brief 按当前 builder 配置构造 `Runtime`。
     */
    Runtime build() const { return Runtime(m_config); }

    /**
     * @brief 导出当前 builder 累积的配置快照。
     */
    RuntimeConfig buildConfig() const { return m_config; }

private:
    RuntimeConfig m_config;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_RUNTIME_H
