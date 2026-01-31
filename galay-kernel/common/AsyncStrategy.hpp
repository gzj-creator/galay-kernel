/**
 * @file AsyncStrategy.hpp
 * @brief 负载均衡策略实现
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供多种负载均衡算法：
 * - RoundRobinLoadBalancer: 轮询（原子操作，天然线程安全）
 * - WeightRoundRobinLoadBalancer: 加权轮询（平滑加权轮询算法）
 * - RandomLoadBalancer: 随机
 * - WeightedRandomLoadBalancer: 加权随机
 *
 * 所有负载均衡器都提供 select() 方法用于选择节点。
 * RoundRobinLoadBalancer 使用原子操作，天然线程安全。
 * 其他负载均衡器的 select() 方法非线程安全，需要外部同步。
 */

#ifndef GALAY_ASYNC_STRATEGY_HPP
#define GALAY_ASYNC_STRATEGY_HPP

#include <atomic>
#include <vector>
#include <memory>
#include <random>
#include <optional>

namespace galay::details
{

/**
 * @brief 轮询负载均衡器
 * @tparam Type 节点类型
 * @details 使用原子操作实现，天然线程安全
 */
template<typename Type>
class RoundRobinLoadBalancer
{
private:
    std::atomic<uint32_t> m_index;
    std::vector<Type> m_nodes;

public:
    using value_type = Type;
    using uptr = std::unique_ptr<RoundRobinLoadBalancer>;
    using ptr = std::shared_ptr<RoundRobinLoadBalancer>;

    RoundRobinLoadBalancer() : m_index(0) {}

    explicit RoundRobinLoadBalancer(const std::vector<Type>& nodes)
        : m_index(0), m_nodes(nodes) {}

    /**
     * @brief 选择下一个节点（线程安全，无锁）
     */
    std::optional<Type> select() {
        if (m_nodes.empty()) {
            return std::nullopt;
        }
        const uint32_t idx = m_index.fetch_add(1, std::memory_order_relaxed);
        return m_nodes[idx % m_nodes.size()];
    }

    size_t size() const { return m_nodes.size(); }

    void append(Type node) {
        m_nodes.emplace_back(std::move(node));
    }
};

/**
 * @brief 加权轮询负载均衡器（平滑加权轮询算法）
 * @tparam Type 节点类型
 */
template<typename Type>
class WeightRoundRobinLoadBalancer
{
private:
    struct alignas(64) Node {
        Type node;
        int32_t current_weight;
        const int32_t fixed_weight;

        Node(Type n, int32_t weight)
            : node(std::move(n)), current_weight(0), fixed_weight(weight) {}
    };

    std::vector<Node> m_nodes;
    int32_t m_total_weight;

public:
    using value_type = Type;
    using uptr = std::unique_ptr<WeightRoundRobinLoadBalancer>;
    using ptr = std::shared_ptr<WeightRoundRobinLoadBalancer>;

    WeightRoundRobinLoadBalancer() : m_total_weight(0) {}

    WeightRoundRobinLoadBalancer(const std::vector<Type>& nodes, const std::vector<uint32_t>& weights)
        : m_total_weight(0)
    {
        if (nodes.size() != weights.size()) {
            m_nodes.reserve(nodes.size());
            for (const auto& n : nodes) {
                m_nodes.emplace_back(n, 1);
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

    /**
     * @brief 选择下一个节点（非线程安全）
     * @note 仅在单线程环境使用，或外部加锁
     */
    std::optional<Type> select() {
        if (m_nodes.empty()) {
            return std::nullopt;
        }

        Node* selected = nullptr;
        for (auto& n : m_nodes) {
            n.current_weight += n.fixed_weight;
            if (selected == nullptr || n.current_weight > selected->current_weight) {
                selected = &n;
            }
        }

        if (selected != nullptr) {
            selected->current_weight -= m_total_weight;
            return selected->node;
        }
        return std::nullopt;
    }

    size_t size() const { return m_nodes.size(); }

    void append(Type node, uint32_t weight) {
        m_nodes.emplace_back(std::move(node), static_cast<int32_t>(weight));
        m_total_weight += static_cast<int32_t>(weight);
    }
};

/**
 * @brief 随机负载均衡器
 * @tparam Type 节点类型
 */
template<typename Type>
class RandomLoadBalancer
{
private:
    std::vector<Type> m_nodes;
    std::mt19937_64 m_rng;

public:
    using value_type = Type;
    using uptr = std::unique_ptr<RandomLoadBalancer>;
    using ptr = std::shared_ptr<RandomLoadBalancer>;

    RandomLoadBalancer() {
        std::random_device rd;
        m_rng.seed(rd());
    }

    explicit RandomLoadBalancer(const std::vector<Type>& nodes) {
        std::random_device rd;
        m_rng.seed(rd());
        m_nodes = nodes;
    }

    /**
     * @brief 随机选择一个节点（非线程安全）
     * @note 仅在单线程环境使用，或外部加锁
     */
    std::optional<Type> select() {
        if (m_nodes.empty()) {
            return std::nullopt;
        }
        std::uniform_int_distribution<size_t> dist(0, m_nodes.size() - 1);
        return m_nodes[dist(m_rng)];
    }

    size_t size() const { return m_nodes.size(); }

    void append(Type node) {
        m_nodes.emplace_back(std::move(node));
    }
};

/**
 * @brief 加权随机负载均衡器
 * @tparam Type 节点类型
 */
template<typename Type>
class WeightedRandomLoadBalancer
{
private:
    struct Node {
        Type node;
        uint32_t weight;

        Node(Type n, uint32_t w) : node(std::move(n)), weight(w) {}
    };

    std::vector<Node> m_nodes;
    uint32_t m_total_weight;
    std::mt19937 m_rng;

public:
    using value_type = Type;
    using uptr = std::unique_ptr<WeightedRandomLoadBalancer>;
    using ptr = std::shared_ptr<WeightedRandomLoadBalancer>;

    WeightedRandomLoadBalancer() : m_total_weight(0) {
        std::random_device rd;
        m_rng.seed(rd());
    }

    WeightedRandomLoadBalancer(const std::vector<Type>& nodes, const std::vector<uint32_t>& weights)
        : m_total_weight(0)
    {
        std::random_device rd;
        m_rng.seed(rd());

        if (nodes.size() != weights.size()) {
            m_nodes.reserve(nodes.size());
            for (const auto& n : nodes) {
                m_nodes.emplace_back(n, 1);
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

    /**
     * @brief 根据权重随机选择一个节点（非线程安全）
     * @note 仅在单线程环境使用，或外部加锁
     */
    std::optional<Type> select() {
        if (m_nodes.empty() || m_total_weight == 0) {
            return std::nullopt;
        }

        std::uniform_int_distribution<uint32_t> dist(1, m_total_weight);
        uint32_t random_weight = dist(m_rng);

        for (const auto& n : m_nodes) {
            if (random_weight <= n.weight) {
                return n.node;
            }
            random_weight -= n.weight;
        }
        return m_nodes.back().node;
    }

    size_t size() const { return m_nodes.size(); }

    void append(Type node, uint32_t weight) {
        m_nodes.emplace_back(std::move(node), weight);
        m_total_weight += weight;
    }
};

} // namespace galay::details

#endif // GALAY_ASYNC_STRATEGY_HPP
