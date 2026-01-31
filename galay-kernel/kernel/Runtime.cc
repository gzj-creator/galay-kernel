/**
 * @file Runtime.cc
 * @brief 运行时调度器管理器实现
 * @author galay-kernel
 * @version 1.0.0
 */

#include "Runtime.h"
#include <thread>

// 根据平台选择默认的 IO 调度器
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

Runtime::Runtime(size_t io_count, size_t compute_count)
    : m_auto_io_count(io_count)
    , m_auto_compute_count(compute_count)
{
}

Runtime::~Runtime()
{
    stop();
}

bool Runtime::addIOScheduler(std::unique_ptr<IOScheduler> scheduler)
{
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }
    m_io_schedulers.push_back(std::move(scheduler));
    return true;
}

bool Runtime::addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler)
{
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }
    m_compute_schedulers.push_back(std::move(scheduler));
    return true;
}

void Runtime::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    // 如果没有手动添加调度器，则自动创建
    if (m_io_schedulers.empty() && m_compute_schedulers.empty()) {
        createDefaultSchedulers();
    }

    // 启动所有 IO 调度器
    for (auto& scheduler : m_io_schedulers) {
        scheduler->start();
    }

    // 启动所有计算调度器
    for (auto& scheduler : m_compute_schedulers) {
        scheduler->start();
    }
}

void Runtime::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    // 按逆序停止计算调度器
    for (auto it = m_compute_schedulers.rbegin(); it != m_compute_schedulers.rend(); ++it) {
        (*it)->stop();
    }

    // 按逆序停止 IO 调度器
    for (auto it = m_io_schedulers.rbegin(); it != m_io_schedulers.rend(); ++it) {
        (*it)->stop();
    }
}

IOScheduler* Runtime::getIOScheduler(size_t index)
{
    if (index >= m_io_schedulers.size()) {
        return nullptr;
    }
    return m_io_schedulers[index].get();
}

ComputeScheduler* Runtime::getComputeScheduler(size_t index)
{
    if (index >= m_compute_schedulers.size()) {
        return nullptr;
    }
    return m_compute_schedulers[index].get();
}

IOScheduler* Runtime::getNextIOScheduler()
{
    if (m_io_schedulers.empty()) {
        return nullptr;
    }
    const uint32_t idx = m_io_index.fetch_add(1, std::memory_order_relaxed);
    return m_io_schedulers[idx % m_io_schedulers.size()].get();
}

ComputeScheduler* Runtime::getNextComputeScheduler()
{
    if (m_compute_schedulers.empty()) {
        return nullptr;
    }
    const uint32_t idx = m_compute_index.fetch_add(1, std::memory_order_relaxed);
    return m_compute_schedulers[idx % m_compute_schedulers.size()].get();
}

size_t Runtime::getCPUCount()
{
    size_t count = std::thread::hardware_concurrency();
    return count > 0 ? count : 4;  // 如果无法获取，默认返回 4
}

void Runtime::createDefaultSchedulers()
{
    // 计算默认数量
    size_t cpu_count = getCPUCount();
    size_t io_count = m_auto_io_count > 0 ? m_auto_io_count : (cpu_count * 2);
    size_t compute_count = m_auto_compute_count > 0 ? m_auto_compute_count : cpu_count;

    // 创建 IO 调度器
    for (size_t i = 0; i < io_count; ++i) {
        m_io_schedulers.push_back(std::make_unique<DefaultIOScheduler>());
    }

    // 创建计算调度器
    for (size_t i = 0; i < compute_count; ++i) {
        m_compute_schedulers.push_back(std::make_unique<ComputeScheduler>());
    }
}

} // namespace galay::kernel
