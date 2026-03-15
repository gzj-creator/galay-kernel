#ifndef GALAY_KERNEL_RUNTIME_H
#define GALAY_KERNEL_RUNTIME_H

#include "BlockingExecutor.h"
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

struct RuntimeAffinityConfig {
    enum class Mode { None, Sequential, Custom } mode = Mode::None;
    size_t seq_io_count = 0;
    size_t seq_compute_count = 0;
    std::vector<uint32_t> custom_io_cpus;
    std::vector<uint32_t> custom_compute_cpus;
};

struct RuntimeConfig {
    size_t io_scheduler_count = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO;
    size_t compute_scheduler_count = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO;
    RuntimeAffinityConfig affinity;
};

class RuntimeHandle;

class Runtime
{
public:
    explicit Runtime(const RuntimeConfig& config = RuntimeConfig{});
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool addIOScheduler(std::unique_ptr<IOScheduler> scheduler);
    bool addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler);

    void start();
    void stop();

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

    static RuntimeHandle current();
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
    RuntimeBuilder& ioSchedulerCount(size_t n)
    {
        m_config.io_scheduler_count = n;
        return *this;
    }

    RuntimeBuilder& computeSchedulerCount(size_t n)
    {
        m_config.compute_scheduler_count = n;
        return *this;
    }

    RuntimeBuilder& sequentialAffinity(size_t ioCount, size_t computeCount)
    {
        m_config.affinity.mode = RuntimeAffinityConfig::Mode::Sequential;
        m_config.affinity.seq_io_count = ioCount;
        m_config.affinity.seq_compute_count = computeCount;
        return *this;
    }

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

    RuntimeBuilder& applyAffinity(const RuntimeAffinityConfig& affinity)
    {
        m_config.affinity = affinity;
        return *this;
    }

    Runtime build() const { return Runtime(m_config); }
    RuntimeConfig buildConfig() const { return m_config; }

private:
    RuntimeConfig m_config;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_RUNTIME_H
