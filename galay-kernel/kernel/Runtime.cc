/**
 * @file Runtime.cc
 * @brief 运行时调度器管理器实现
 * @author galay-kernel
 * @version 1.0.0
 */

#include "Runtime.h"
#include <algorithm>

namespace galay::kernel
{

Runtime::Runtime(LoadBalanceStrategy strategy)
    : m_strategy(strategy)
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

    std::lock_guard<std::mutex> lock(m_mutex);
    m_io_schedulers.push_back(std::move(scheduler));
    return true;
}

bool Runtime::addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler)
{
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_compute_schedulers.push_back(std::move(scheduler));
    return true;
}

void Runtime::initLoadBalancers()
{
    // 构建 IO 调度器指针列表
    std::vector<IOScheduler*> io_ptrs;
    io_ptrs.reserve(m_io_schedulers.size());
    for (auto& scheduler : m_io_schedulers) {
        io_ptrs.push_back(scheduler.get());
    }

    // 构建计算调度器指针列表
    std::vector<ComputeScheduler*> compute_ptrs;
    compute_ptrs.reserve(m_compute_schedulers.size());
    for (auto& scheduler : m_compute_schedulers) {
        compute_ptrs.push_back(scheduler.get());
    }

    // 根据策略初始化负载均衡器
    switch (m_strategy) {
        case LoadBalanceStrategy::ROUND_ROBIN:
            m_io_load_balancer = std::make_unique<IOLoadBalancer>(
                std::in_place_index<0>, io_ptrs
            );
            m_compute_load_balancer = std::make_unique<ComputeLoadBalancer>(
                std::in_place_index<0>, compute_ptrs
            );
            break;

        case LoadBalanceStrategy::RANDOM:
            m_io_load_balancer = std::make_unique<IOLoadBalancer>(
                std::in_place_index<1>, io_ptrs
            );
            m_compute_load_balancer = std::make_unique<ComputeLoadBalancer>(
                std::in_place_index<1>, compute_ptrs
            );
            break;
    }
}

void Runtime::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 启动所有 IO 调度器
    for (auto& scheduler : m_io_schedulers) {
        scheduler->start();
    }

    // 启动所有计算调度器
    for (auto& scheduler : m_compute_schedulers) {
        scheduler->start();
    }

    // 初始化负载均衡器
    initLoadBalancers();
}

void Runtime::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 按逆序停止计算调度器
    for (auto it = m_compute_schedulers.rbegin(); it != m_compute_schedulers.rend(); ++it) {
        (*it)->stop();
    }

    // 按逆序停止 IO 调度器
    for (auto it = m_io_schedulers.rbegin(); it != m_io_schedulers.rend(); ++it) {
        (*it)->stop();
    }

    // 清理负载均衡器
    m_io_load_balancer.reset();
    m_compute_load_balancer.reset();
}

IOScheduler* Runtime::getIOScheduler(size_t index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_io_schedulers.size()) {
        return nullptr;
    }
    return m_io_schedulers[index].get();
}

ComputeScheduler* Runtime::getComputeScheduler(size_t index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_compute_schedulers.size()) {
        return nullptr;
    }
    return m_compute_schedulers[index].get();
}

IOScheduler* Runtime::getNextIOScheduler()
{
    if (!m_io_load_balancer) {
        return nullptr;
    }

    // 使用 std::visit 调用对应的负载均衡器
    return std::visit([](auto& balancer) -> IOScheduler* {
        auto result = balancer.select();
        return result.has_value() ? result.value() : nullptr;
    }, *m_io_load_balancer);
}

ComputeScheduler* Runtime::getNextComputeScheduler()
{
    if (!m_compute_load_balancer) {
        return nullptr;
    }

    // 使用 std::visit 调用对应的负载均衡器
    return std::visit([](auto& balancer) -> ComputeScheduler* {
        auto result = balancer.select();
        return result.has_value() ? result.value() : nullptr;
    }, *m_compute_load_balancer);
}

} // namespace galay::kernel
