/**
 * @file Strategy.hpp
 * @brief 负载均衡策略实现
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供多种负载均衡算法：
 * - RoundRobinLoadBalancer: 轮询（线程安全）
 * - WeightRoundRobinLoadBalancer: 加权轮询（平滑加权轮询算法）
 * - RandomLoadBalancer: 随机
 * - WeightedRandomLoadBalancer: 加权随机
 */

#ifndef GALAY_STRATEGY_HPP
#define GALAY_STRATEGY_HPP

#include <atomic>
#include <vector>
#include <memory>
#include <random>
#include <optional>
#include <mutex>

namespace galay::details
{

/**
 * @brief 轮询负载均衡器（线程安全）
 * @tparam Type 节点类型
 */
template<typename Type>
class RoundRobinLoadBalancer
{
public:
    using value_type = Type;
    using uptr = std::unique_ptr<RoundRobinLoadBalancer>;
    using ptr = std::shared_ptr<RoundRobinLoadBalancer>;

    /**
     * @brief 构造函数
     * @param nodes 节点列表
     */
    RoundRobinLoadBalancer(const std::vector<Type>& nodes)
        : m_index(0), m_nodes(nodes) {}

    /**
     * @brief 选择下一个节点
     * @return 选中的节点，如果没有节点返回 nullopt
     */
    std::optional<Type> select();

    /**
     * @brief 获取节点数量
     */
    size_t size() const { return m_nodes.size(); }

    /**
     * @brief 添加节点
     * @param node 要添加的节点
     * @note 非线程安全，应在初始化时调用
     */
    void append(Type node);

private:
    std::atomic<uint32_t> m_index;
    std::vector<Type> m_nodes;
};

/**
 * @brief 加权轮询负载均衡器（平滑加权轮询算法）
 * @tparam Type 节点类型
 * @details 使用 Nginx 的平滑加权轮询算法，保证散列均匀
 */
template<typename Type>
class WeightRoundRobinLoadBalancer
{
    struct alignas(64) Node { // 缓存行对齐，避免伪共享
        Type node;
        int32_t current_weight;
        const int32_t fixed_weight;

        Node(Type n, int32_t weight)
            : node(std::move(n)), current_weight(0), fixed_weight(weight) {}
    };

public:
    using value_type = Type;
    using uptr = std::unique_ptr<WeightRoundRobinLoadBalancer>;
    using ptr = std::shared_ptr<WeightRoundRobinLoadBalancer>;

    /**
     * @brief 构造函数
     * @param nodes 节点列表
     * @param weights 权重列表（必须与 nodes 长度相同）
     */
    WeightRoundRobinLoadBalancer(const std::vector<Type>& nodes, const std::vector<uint32_t>& weights);

    /**
     * @brief 选择下一个节点
     * @return 选中的节点，如果没有节点返回 nullopt
     * @note 线程安全
     */
    std::optional<Type> select();

    /**
     * @brief 获取节点数量
     */
    size_t size() const { return m_nodes.size(); }

    /**
     * @brief 添加节点
     * @param node 要添加的节点
     * @param weight 节点权重
     * @note 非线程安全，应在初始化时调用
     */
    void append(Type node, uint32_t weight);

private:
    std::vector<Node> m_nodes;
    int32_t m_total_weight;
    std::mutex m_mutex;  // 保护 select 操作
};

/**
 * @brief 随机负载均衡器
 * @tparam Type 节点类型
 */
template<typename Type>
class RandomLoadBalancer
{
public:
    using value_type = Type;
    using uptr = std::unique_ptr<RandomLoadBalancer>;
    using ptr = std::shared_ptr<RandomLoadBalancer>;

    /**
     * @brief 构造函数
     * @param nodes 节点列表
     */
    RandomLoadBalancer(const std::vector<Type>& nodes);

    /**
     * @brief 随机选择一个节点
     * @return 选中的节点，如果没有节点返回 nullopt
     * @note 线程安全
     */
    std::optional<Type> select();

    /**
     * @brief 获取节点数量
     */
    size_t size() const { return m_nodes.size(); }

    /**
     * @brief 添加节点
     * @param node 要添加的节点
     * @note 非线程安全，应在初始化时调用
     */
    void append(Type node);

private:
    std::vector<Type> m_nodes;
    std::mt19937_64 m_rng;
    std::mutex m_mutex;  // 保护随机数生成器
};

/**
 * @brief 加权随机负载均衡器
 * @tparam Type 节点类型
 */
template<typename Type>
class WeightedRandomLoadBalancer
{
    struct Node {
        Type node;
        uint32_t weight;

        Node(Type n, uint32_t w) : node(std::move(n)), weight(w) {}
    };

public:
    using value_type = Type;
    using uptr = std::unique_ptr<WeightedRandomLoadBalancer>;
    using ptr = std::shared_ptr<WeightedRandomLoadBalancer>;

    /**
     * @brief 构造函数
     * @param nodes 节点列表
     * @param weights 权重列表（必须与 nodes 长度相同）
     */
    WeightedRandomLoadBalancer(const std::vector<Type>& nodes, const std::vector<uint32_t>& weights);

    /**
     * @brief 根据权重随机选择一个节点
     * @return 选中的节点，如果没有节点返回 nullopt
     * @note 线程安全
     */
    std::optional<Type> select();

    /**
     * @brief 获取节点数量
     */
    size_t size() const { return m_nodes.size(); }

    /**
     * @brief 添加节点
     * @param node 要添加的节点
     * @param weight 节点权重
     * @note 非线程安全，应在初始化时调用
     */
    void append(Type node, uint32_t weight);

private:
    std::vector<Node> m_nodes;
    uint32_t m_total_weight;
    std::mt19937 m_rng;
    std::mutex m_mutex;  // 保护随机数生成器和节点访问
};

}

#include "Strategy.inl"

#endif // GALAY_STRATEGY_HPP
