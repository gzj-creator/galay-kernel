#ifndef GALAY_KERNEL_RUNTIME_H
#define GALAY_KERNEL_RUNTIME_H

#include "IOScheduler.hpp"
#include "ComputeScheduler.h"
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>

namespace galay::kernel
{

#define GALAY_RUNTIME_SCHEDULER_COUNT_AUTO static_cast<size_t>(-1)

// ─── Affinity config ────────────────────────────────────────────────────────

struct RuntimeAffinityConfig {
    enum class Mode { None, Sequential, Custom } mode = Mode::None;
    size_t seq_io_count     = 0;
    size_t seq_compute_count = 0;
    std::vector<uint32_t> custom_io_cpus;
    std::vector<uint32_t> custom_compute_cpus;
};

// ─── Runtime config ─────────────────────────────────────────────────────────

struct RuntimeConfig {
    size_t io_scheduler_count     = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO;  ///< auto = 2*CPU, 0 = disabled
    size_t compute_scheduler_count = GALAY_RUNTIME_SCHEDULER_COUNT_AUTO; ///< auto = CPU, 0 = disabled
    RuntimeAffinityConfig affinity;
};

// ─── Runtime ────────────────────────────────────────────────────────────────

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

    bool   isRunning()             const { return m_running.load(std::memory_order_acquire); }
    size_t getIOSchedulerCount()   const { return m_io_schedulers.size(); }
    size_t getComputeSchedulerCount() const { return m_compute_schedulers.size(); }

    IOScheduler*     getIOScheduler(size_t index);
    ComputeScheduler* getComputeScheduler(size_t index);
    IOScheduler*     getNextIOScheduler();
    ComputeScheduler* getNextComputeScheduler();

private:
    void createDefaultSchedulers();
    void applyAffinityConfig();
    static size_t getCPUCount();

    std::vector<std::unique_ptr<IOScheduler>>     m_io_schedulers;
    std::vector<std::unique_ptr<ComputeScheduler>> m_compute_schedulers;

    std::atomic<uint32_t> m_io_index{0};
    std::atomic<uint32_t> m_compute_index{0};

    RuntimeConfig m_config;
    std::atomic<bool> m_running{false};
};

// ─── RuntimeBuilder ─────────────────────────────────────────────────────────

class RuntimeBuilder {
public:
    RuntimeBuilder& ioSchedulerCount(size_t n)     { m_config.io_scheduler_count = n;      return *this; }
    RuntimeBuilder& computeSchedulerCount(size_t n) { m_config.compute_scheduler_count = n; return *this; }

    /// 顺序绑核：从 0 开始依次分配，超出 CPU 数量后回绕
    RuntimeBuilder& sequentialAffinity(size_t io_count, size_t compute_count) {
        m_config.affinity.mode            = RuntimeAffinityConfig::Mode::Sequential;
        m_config.affinity.seq_io_count     = io_count;
        m_config.affinity.seq_compute_count = compute_count;
        return *this;
    }

    /// 自定义绑核：向量长度必须与调度器数量严格一致，否则返回 false 且不修改配置
    bool customAffinity(std::vector<uint32_t> io_cpus, std::vector<uint32_t> compute_cpus) {
        if (io_cpus.size() != m_config.io_scheduler_count ||
            compute_cpus.size() != m_config.compute_scheduler_count) {
            return false;
        }
        m_config.affinity.mode             = RuntimeAffinityConfig::Mode::Custom;
        m_config.affinity.custom_io_cpus   = std::move(io_cpus);
        m_config.affinity.custom_compute_cpus = std::move(compute_cpus);
        return true;
    }

    /// 直接应用已有的 RuntimeAffinityConfig（供 ServerBuilder 等透传）
    RuntimeBuilder& applyAffinity(const RuntimeAffinityConfig& aff) {
        m_config.affinity = aff;
        return *this;
    }

    Runtime     build()       const { return Runtime(m_config); }
    RuntimeConfig buildConfig() const { return m_config; }

private:
    RuntimeConfig m_config;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_RUNTIME_H
