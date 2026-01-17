/**
 * @file Runtime.h
 * @brief 运行时调度器管理器
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 在运行时加载和管理多个 Scheduler，包括 IO 调度器和计算调度器。
 * 提供统一的启动、停止和调度器获取接口，支持多种负载均衡策略。
 *
 * 使用方式：
 * @code
 * // 方式1: 零配置启动（自动创建默认数量的调度器）
 * Runtime runtime;
 * runtime.start();  // 自动创建 2*CPU 核心数的 IO 调度器和 CPU 核心数的计算调度器
 *
 * // 方式2: 指定调度器数量
 * Runtime runtime(LoadBalanceStrategy::ROUND_ROBIN, 4, 8);  // 4 个 IO 调度器，8 个计算调度器
 * runtime.start();
 *
 * // 方式3: 手动添加调度器
 * Runtime runtime;
 * auto io_scheduler = std::make_unique<EpollScheduler>();
 * runtime.addIOScheduler(std::move(io_scheduler));
 * auto compute_scheduler = std::make_unique<ComputeScheduler>();
 * runtime.addComputeScheduler(std::move(compute_scheduler));
 * runtime.start();
 *
 * // 获取调度器（使用负载均衡策略）
 * auto* io = runtime.getNextIOScheduler();
 * auto* compute = runtime.getNextComputeScheduler();
 *
 * // 停止所有调度器
 * runtime.stop();
 * @endcode
 */

#ifndef GALAY_KERNEL_RUNTIME_H
#define GALAY_KERNEL_RUNTIME_H

#include "IOScheduler.hpp"
#include "ComputeScheduler.h"
#include "galay-kernel/common/Strategy.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <variant>

namespace galay::kernel
{

/**
 * @brief 负载均衡策略枚举
 */
enum class LoadBalanceStrategy {
    ROUND_ROBIN,        ///< 轮询
    RANDOM              ///< 随机
};

/**
 * @brief 运行时调度器管理器
 *
 * @details 管理多个 IO 调度器和计算调度器的生命周期。
 * 特点：
 * - 支持自动创建默认数量的调度器（基于 CPU 核心数）
 * - 支持手动添加调度器（启动前）
 * - 统一启动和停止所有调度器
 * - 支持多种负载均衡策略（轮询、随机）
 * - 线程安全的调度器访问
 *
 * @note
 * - 如果不手动添加调度器，start() 会自动创建默认数量的调度器
 * - 调度器应在 start() 之前添加
 * - start() 后不应再添加新的调度器
 * - stop() 会等待所有调度器停止
 */
class Runtime
{
public:
    /**
     * @brief 构造函数（自动配置模式）
     * @param strategy 负载均衡策略，默认为轮询
     * @param io_count IO 调度器数量，0 表示自动（2 * CPU 核心数）
     * @param compute_count 计算调度器数量，0 表示自动（CPU 核心数）
     * @note 如果指定了数量，会在 start() 时自动创建对应数量的调度器
     */
    explicit Runtime(
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN,
        size_t io_count = 0,
        size_t compute_count = 0
    );

    /**
     * @brief 析构函数
     * @note 会自动调用 stop()
     */
    ~Runtime();

    // 禁止拷贝
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * @brief 添加 IO 调度器
     * @param scheduler IO 调度器的唯一指针
     * @return true 添加成功，false 添加失败（运行时不允许添加）
     * @note 必须在 start() 之前调用
     */
    bool addIOScheduler(std::unique_ptr<IOScheduler> scheduler);

    /**
     * @brief 添加计算调度器
     * @param scheduler 计算调度器的唯一指针
     * @return true 添加成功，false 添加失败（运行时不允许添加）
     * @note 必须在 start() 之前调用
     */
    bool addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler);

    /**
     * @brief 启动所有调度器
     * @note 按添加顺序启动所有调度器，并初始化负载均衡器
     */
    void start();

    /**
     * @brief 停止所有调度器
     * @note 按添加顺序的逆序停止所有调度器
     */
    void stop();

    /**
     * @brief 检查运行时是否正在运行
     * @return true 如果正在运行
     */
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    /**
     * @brief 获取 IO 调度器数量
     * @return IO 调度器数量
     */
    size_t getIOSchedulerCount() const { return m_io_schedulers.size(); }

    /**
     * @brief 获取计算调度器数量
     * @return 计算调度器数量
     */
    size_t getComputeSchedulerCount() const { return m_compute_schedulers.size(); }

    /**
     * @brief 根据索引获取 IO 调度器
     * @param index 调度器索引
     * @return IO 调度器指针，如果索引越界返回 nullptr
     */
    IOScheduler* getIOScheduler(size_t index);

    /**
     * @brief 根据索引获取计算调度器
     * @param index 调度器索引
     * @return 计算调度器指针，如果索引越界返回 nullptr
     */
    ComputeScheduler* getComputeScheduler(size_t index);

    /**
     * @brief 使用负载均衡策略获取下一个 IO 调度器
     * @return IO 调度器指针，如果没有调度器返回 nullptr
     * @note 根据构造时指定的策略进行负载均衡
     */
    IOScheduler* getNextIOScheduler();

    /**
     * @brief 使用负载均衡策略获取下一个计算调度器
     * @return 计算调度器指针，如果没有调度器返回 nullptr
     * @note 根据构造时指定的策略进行负载均衡
     */
    ComputeScheduler* getNextComputeScheduler();

    /**
     * @brief 获取当前使用的负载均衡策略
     * @return 负载均衡策略
     */
    LoadBalanceStrategy getLoadBalanceStrategy() const { return m_strategy; }

private:
    /**
     * @brief 初始化负载均衡器
     */
    void initLoadBalancers();

    /**
     * @brief 创建默认的调度器
     */
    void createDefaultSchedulers();

    /**
     * @brief 获取 CPU 核心数
     */
    static size_t getCPUCount();

private:
    using IOLoadBalancer = std::variant<
        details::RoundRobinLoadBalancer<IOScheduler*>,
        details::RandomLoadBalancer<IOScheduler*>
    >;

    using ComputeLoadBalancer = std::variant<
        details::RoundRobinLoadBalancer<ComputeScheduler*>,
        details::RandomLoadBalancer<ComputeScheduler*>
    >;

    std::vector<std::unique_ptr<IOScheduler>> m_io_schedulers;           ///< IO 调度器列表
    std::vector<std::unique_ptr<ComputeScheduler>> m_compute_schedulers; ///< 计算调度器列表

    LoadBalanceStrategy m_strategy;                                      ///< 负载均衡策略
    std::unique_ptr<IOLoadBalancer> m_io_load_balancer;                 ///< IO 调度器负载均衡器
    std::unique_ptr<ComputeLoadBalancer> m_compute_load_balancer;       ///< 计算调度器负载均衡器

    size_t m_auto_io_count;                                              ///< 自动创建的 IO 调度器数量（0 表示不自动创建）
    size_t m_auto_compute_count;                                         ///< 自动创建的计算调度器数量（0 表示不自动创建）

    std::atomic<bool> m_running{false};                                  ///< 运行状态
    mutable std::mutex m_mutex;                                          ///< 保护调度器列表的互斥锁
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_RUNTIME_H
