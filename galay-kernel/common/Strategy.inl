/**
 * @file Strategy.inl
 * @brief 负载均衡策略实现
 * @author galay-kernel
 * @version 1.0.0
 */

#ifndef GALAY_STRATEGY_INL
#define GALAY_STRATEGY_INL

#include "Strategy.hpp"
#include <algorithm>

namespace galay::details
{

// ==================== RoundRobinLoadBalancer ====================

template<typename Type>
inline std::optional<Type> RoundRobinLoadBalancer<Type>::select()
{
    if (m_nodes.empty()) {
        return std::nullopt;
    }
    const uint32_t idx = m_index.fetch_add(1, std::memory_order_relaxed);
    return m_nodes[idx % m_nodes.size()];
}

template<typename Type>
inline void RoundRobinLoadBalancer<Type>::append(Type node)
{
    m_nodes.emplace_back(std::move(node));
}

// ==================== WeightRoundRobinLoadBalancer ====================

template<typename Type>
inline WeightRoundRobinLoadBalancer<Type>::WeightRoundRobinLoadBalancer(
    const std::vector<Type>& nodes,
    const std::vector<uint32_t>& weights)
    : m_total_weight(0)
{
    if (nodes.size() != weights.size()) {
        // 节点和权重数量不匹配，使用默认权重 1
        m_nodes.reserve(nodes.size());
        for (const auto& node : nodes) {
            m_nodes.emplace_back(node, 1);
            m_total_weight += 1;
        }
    } else {
        m_nodes.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            m_nodes.emplace_back(nodes[i], static_cast<int32_t>(weights[i]));
            m_total_weight += static_cast<int32_t>(weights[i]);
        }
    }
}

template<typename Type>
inline std::optional<Type> WeightRoundRobinLoadBalancer<Type>::select()
{
    if (m_nodes.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Nginx 平滑加权轮询算法
    Node* selected = nullptr;

    // 1. 遍历所有节点，累加 current_weight
    for (auto& node : m_nodes) {
        node.current_weight += node.fixed_weight;

        // 2. 选择 current_weight 最大的节点
        if (selected == nullptr || node.current_weight > selected->current_weight) {
            selected = &node;
        }
    }

    // 3. 选中节点的 current_weight 减去总权重
    if (selected != nullptr) {
        selected->current_weight -= m_total_weight;
        return selected->node;
    }

    return std::nullopt;
}

template<typename Type>
inline void WeightRoundRobinLoadBalancer<Type>::append(Type node, uint32_t weight)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodes.emplace_back(std::move(node), static_cast<int32_t>(weight));
    m_total_weight += static_cast<int32_t>(weight);
}

// ==================== RandomLoadBalancer ====================

template<typename Type>
inline RandomLoadBalancer<Type>::RandomLoadBalancer(const std::vector<Type>& nodes)
    : m_nodes(nodes)
{
    std::random_device rd;
    m_rng.seed(rd());
}

template<typename Type>
inline std::optional<Type> RandomLoadBalancer<Type>::select()
{
    if (m_nodes.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::uniform_int_distribution<size_t> dist(0, m_nodes.size() - 1);
    return m_nodes[dist(m_rng)];
}

template<typename Type>
inline void RandomLoadBalancer<Type>::append(Type node)
{
    m_nodes.emplace_back(std::move(node));
}

// ==================== WeightedRandomLoadBalancer ====================

template<typename Type>
inline WeightedRandomLoadBalancer<Type>::WeightedRandomLoadBalancer(
    const std::vector<Type>& nodes,
    const std::vector<uint32_t>& weights)
    : m_total_weight(0)
{
    std::random_device rd;
    m_rng.seed(rd());

    if (nodes.size() != weights.size()) {
        // 节点和权重数量不匹配，使用默认权重 1
        m_nodes.reserve(nodes.size());
        for (const auto& node : nodes) {
            m_nodes.emplace_back(node, 1);
            m_total_weight += 1;
        }
    } else {
        m_nodes.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            m_nodes.emplace_back(nodes[i], weights[i]);
            m_total_weight += weights[i];
        }
    }
}

template<typename Type>
inline std::optional<Type> WeightedRandomLoadBalancer<Type>::select()
{
    if (m_nodes.empty() || m_total_weight == 0) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 生成 [1, m_total_weight] 范围内的随机数
    std::uniform_int_distribution<uint32_t> dist(1, m_total_weight);
    uint32_t random_weight = dist(m_rng);

    // 遍历节点，累减权重
    for (const auto& node : m_nodes) {
        if (random_weight <= node.weight) {
            return node.node;
        }
        random_weight -= node.weight;
    }

    // 理论上不应该到达这里
    return m_nodes.back().node;
}

template<typename Type>
inline void WeightedRandomLoadBalancer<Type>::append(Type node, uint32_t weight)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodes.emplace_back(std::move(node), weight);
    m_total_weight += weight;
}

}

#endif // GALAY_STRATEGY_INL
