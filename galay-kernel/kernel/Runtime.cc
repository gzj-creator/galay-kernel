/**
 * @file Runtime.cc
 * @brief 运行时调度器管理器实现
 */

#include "Runtime.h"
#include "TimerScheduler.h"
#include <thread>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include "KqueueScheduler.h"
using DefaultIOScheduler = galay::kernel::KqueueScheduler;
#elif defined(__linux__)
#ifdef USE_IOURING
#include "IOUringScheduler.h"
using DefaultIOScheduler = galay::kernel::IOUringScheduler;
#else
#include "EpollScheduler.h"
using DefaultIOScheduler = galay::kernel::EpollScheduler;
#endif
#endif

namespace galay::kernel
{

Runtime::Runtime(const RuntimeConfig& config)
    : m_config(config)
{
}

Runtime::~Runtime()
{
    stop();
}

bool Runtime::addIOScheduler(std::unique_ptr<IOScheduler> scheduler)
{
    if (m_running.load(std::memory_order_acquire)) return false;
    m_io_schedulers.push_back(std::move(scheduler));
    return true;
}

bool Runtime::addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler)
{
    if (m_running.load(std::memory_order_acquire)) return false;
    m_compute_schedulers.push_back(std::move(scheduler));
    return true;
}

void Runtime::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

    if (m_io_schedulers.empty() && m_compute_schedulers.empty()) {
        createDefaultSchedulers();
    }

    applyAffinityConfig();

    TimerScheduler::getInstance()->start();
    for (auto& s : m_io_schedulers)      s->start();
    for (auto& s : m_compute_schedulers) s->start();
}

void Runtime::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) return;

    for (auto it = m_compute_schedulers.rbegin(); it != m_compute_schedulers.rend(); ++it) (*it)->stop();
    for (auto it = m_io_schedulers.rbegin();      it != m_io_schedulers.rend();      ++it) (*it)->stop();
    TimerScheduler::getInstance()->stop();
}

IOScheduler* Runtime::getIOScheduler(size_t index)
{
    return index < m_io_schedulers.size() ? m_io_schedulers[index].get() : nullptr;
}

ComputeScheduler* Runtime::getComputeScheduler(size_t index)
{
    return index < m_compute_schedulers.size() ? m_compute_schedulers[index].get() : nullptr;
}

IOScheduler* Runtime::getNextIOScheduler()
{
    if (m_io_schedulers.empty()) return nullptr;
    return m_io_schedulers[m_io_index.fetch_add(1, std::memory_order_relaxed) % m_io_schedulers.size()].get();
}

ComputeScheduler* Runtime::getNextComputeScheduler()
{
    if (m_compute_schedulers.empty()) return nullptr;
    return m_compute_schedulers[m_compute_index.fetch_add(1, std::memory_order_relaxed) % m_compute_schedulers.size()].get();
}

size_t Runtime::getCPUCount()
{
    size_t n = std::thread::hardware_concurrency();
    return n > 0 ? n : 4;
}

void Runtime::createDefaultSchedulers()
{
    size_t cpu = getCPUCount();
    const size_t io = m_config.io_scheduler_count == GALAY_RUNTIME_SCHEDULER_COUNT_AUTO
        ? cpu * 2
        : m_config.io_scheduler_count;
    const size_t cmp = m_config.compute_scheduler_count == GALAY_RUNTIME_SCHEDULER_COUNT_AUTO
        ? cpu
        : m_config.compute_scheduler_count;

    for (size_t i = 0; i < io;  ++i) m_io_schedulers.push_back(std::make_unique<DefaultIOScheduler>());
    for (size_t i = 0; i < cmp; ++i) m_compute_schedulers.push_back(std::make_unique<ComputeScheduler>());
}

void Runtime::applyAffinityConfig()
{
    const auto& aff = m_config.affinity;
    if (aff.mode == RuntimeAffinityConfig::Mode::None) return;

    const uint32_t cpu_count = static_cast<uint32_t>(getCPUCount());

    if (aff.mode == RuntimeAffinityConfig::Mode::Sequential) {
        uint32_t cpu = 0;
        for (size_t i = 0; i < aff.seq_io_count && i < m_io_schedulers.size(); ++i) {
            m_io_schedulers[i]->setAffinity(cpu % cpu_count);
            ++cpu;
        }
        cpu = 0;
        for (size_t i = 0; i < aff.seq_compute_count && i < m_compute_schedulers.size(); ++i) {
            m_compute_schedulers[i]->setAffinity(cpu % cpu_count);
            ++cpu;
        }
        return;
    }

    // Custom: strict — sizes already validated in RuntimeBuilder::customAffinity
    if (aff.custom_io_cpus.size() != m_io_schedulers.size() ||
        aff.custom_compute_cpus.size() != m_compute_schedulers.size()) {
        return; // mismatch: apply nothing
    }
    for (size_t i = 0; i < m_io_schedulers.size(); ++i)
        m_io_schedulers[i]->setAffinity(aff.custom_io_cpus[i]);
    for (size_t i = 0; i < m_compute_schedulers.size(); ++i)
        m_compute_schedulers[i]->setAffinity(aff.custom_compute_cpus[i]);
}

} // namespace galay::kernel
