#ifndef GALAY_KERNEL_IOCONTROLLER_HPP
#define GALAY_KERNEL_IOCONTROLLER_HPP

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Host.hpp"

#ifdef USE_IOURING
#include <atomic>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#endif

namespace galay::kernel
{

struct SequenceAwaitableBase;

/**
 * @brief IO事件控制器
 *
 * @details 管理单个IO操作的状态和回调。
 * 每个异步IO操作都关联一个IOController。
 *
 * @note
 * - 存储当前IO操作类型和对应的Awaitable对象
 * - 支持超时机制，通过 generation 和 state 防止重复唤醒
 * - 支持同时 RECV 和 SEND
 * - !!! 不允许跨调度器执行(非线程安全)
 */
struct IOController;

#ifdef USE_IOURING
/**
 * @brief SQE 标签，生命周期与 IOController 绑定
 * @details 用作 io_uring sqe 的 user_data，避免超时销毁 awaitable 后 CQE 解引用野指针
 */
struct SqeState {
    /**
     * @brief 构造 SQE 状态对象
     * @param controller 所属的 IOController
     * @param index 对应的 READ/WRITE 槽位
     */
    explicit SqeState(IOController* controller, uint8_t index)
        : owner(controller)
        , slot(index) {}

    std::atomic<IOController*> owner;  ///< 当前拥有该 SQE 槽位的 IOController
    const uint8_t slot;  ///< READ/WRITE 槽位编号
    std::atomic<uint64_t> generation{1};  ///< 每次重绑或失效时递增，用于过滤过期 CQE
};

/**
 * @brief io_uring 请求令牌
 * @details 作为提交到 reactor 的 user_data 包装，携带共享状态和对应 generation。
 */
struct SqeTokenArena;

struct SqeRequestToken {
    std::shared_ptr<SqeState> state;  ///< SQE 共享状态
    std::shared_ptr<SqeTokenArena> arena;  ///< 持有令牌池生命周期，保证晚到 CQE 仍可安全回收
    uint64_t generation = 0;  ///< 本次提交时观测到的 generation
    bool persistent = false;  ///< 是否绑定到会产生多次 CQE 的持久请求
    SqeRequestToken* next_free = nullptr;  ///< 空闲链表指针

    void recycle() noexcept;  ///< 将令牌归还到所属池
};

/**
 * @brief io_uring 请求令牌池
 * @details 预分配稳定地址的 token 块，避免热路径 `new/delete`，并允许晚到 CQE 在控制器迁移后安全回收。
 */
struct SqeTokenArena {
    /**
     * @brief 构造令牌池
     * @details 初始化首个令牌块，保证常规热路径可直接获取 token。
     */
    SqeTokenArena() {
        grow();
    }

    SqeTokenArena(const SqeTokenArena&) = delete;
    SqeTokenArena& operator=(const SqeTokenArena&) = delete;

    /**
     * @brief 从池中借出一个请求令牌
     * @param state 当前请求对应的共享 SQE 状态
     * @param self 当前池的共享所有权，用于让 in-flight token 保活池对象
     * @return 成功返回稳定地址 token；扩容失败或状态缺失时返回 nullptr
     */
    SqeRequestToken* acquire(const std::shared_ptr<SqeState>& state,
                             const std::shared_ptr<SqeTokenArena>& self) {
        if (!state) {
            return nullptr;
        }
        if (m_free == nullptr) {
            if (!grow()) {
                return nullptr;
            }
        }

        auto* token = m_free;
        m_free = token->next_free;
        token->next_free = nullptr;
        token->state = state;
        token->arena = self;
        token->generation = state->generation.load(std::memory_order_acquire);
        return token;
    }

    /**
     * @brief 回收请求令牌
     * @param token 待归还的 token；允许为空
     */
    void recycle(SqeRequestToken* token) noexcept {
        if (token == nullptr) {
            return;
        }
        token->state.reset();
        token->arena.reset();
        token->generation = 0;
        token->persistent = false;
        token->next_free = m_free;
        m_free = token;
    }

private:
    struct Block {
        static constexpr size_t kTokenCount = 8;

        std::unique_ptr<Block> next;  ///< 下一块令牌存储
        SqeRequestToken tokens[kTokenCount];  ///< 稳定地址 token 数组
    };

    bool grow() {
        auto block = std::make_unique<Block>();
        if (!block) {
            return false;
        }
        for (auto& token : block->tokens) {
            token.next_free = m_free;
            m_free = &token;
        }
        block->next = std::move(m_blocks);
        m_blocks = std::move(block);
        return true;
    }

    std::unique_ptr<Block> m_blocks;  ///< 所有已分配 token 块
    SqeRequestToken* m_free = nullptr;  ///< 空闲 token 单链表头
};

inline void SqeRequestToken::recycle() noexcept {
    if (arena) {
        arena->recycle(this);
    }
}
#endif

struct IOController {
    /**
     * @brief IO操作索引（用于数组访问）
     */
    enum Index : uint8_t {
        READ = 0,             ///< Read 操作
        WRITE = 1,            ///< Write 操作
        SIZE                  ///< 槽位数量
    };

    /**
     * @brief 构造 IO 控制器
     * @param handle 关联的底层句柄
     */
    IOController(GHandle handle)
        : m_handle(handle)
#ifdef USE_IOURING
        , m_sqe_state{
            std::make_shared<SqeState>(this, READ),
            std::make_shared<SqeState>(this, WRITE)}
        , m_sqe_token_pool{
            std::make_shared<SqeTokenArena>(),
            std::make_shared<SqeTokenArena>()}
#endif
    {}

    /**
     * @brief 析构 IO 控制器
     * @note 在 io_uring 模式下会主动使历史 SQE 请求失效
     */
    ~IOController() {
#ifdef USE_IOURING
        clearSqeState();
#endif
    }

    IOController(const IOController&) = delete;
    IOController& operator=(const IOController&) = delete;

    /**
     * @brief 移动构造 IO 控制器
     * @param other 被移动的控制器
     * @note io_uring 状态会重绑到当前对象，源对象会被重置为 moved-from 状态
     */
    IOController(IOController&& other) noexcept
        : m_handle(other.m_handle)
        , m_type(other.m_type)
        , m_awaitable{other.m_awaitable[READ], other.m_awaitable[WRITE]}
        , m_sequence_owner{other.m_sequence_owner[READ], other.m_sequence_owner[WRITE]}
        , m_sequence_interest_mask(other.m_sequence_interest_mask)
        , m_sequence_armed_mask(other.m_sequence_armed_mask)
#ifdef USE_EPOLL
        , m_registered_events(other.m_registered_events)
#endif
#ifdef USE_IOURING
        , m_sqe_state{std::move(other.m_sqe_state[READ]), std::move(other.m_sqe_state[WRITE])}
        , m_sqe_token_pool{
            std::move(other.m_sqe_token_pool[READ]),
            std::move(other.m_sqe_token_pool[WRITE])}
        , m_ready_accepts(std::move(other.m_ready_accepts))
        , m_accept_multishot_token(other.m_accept_multishot_token)
        , m_accept_multishot_armed(other.m_accept_multishot_armed)
        , m_accept_result_assigned(other.m_accept_result_assigned)
#endif
    {
#ifdef USE_IOURING
        rebindSqeState();
#endif
        other.resetMovedFrom();
    }

    /**
     * @brief 移动赋值 IO 控制器
     * @param other 被移动的控制器
     * @return 当前对象引用
     * @note io_uring 状态会重绑到当前对象，源对象会被重置为 moved-from 状态
     */
    IOController& operator=(IOController&& other) noexcept {
        if (this != &other) {
            m_handle = other.m_handle;
            m_type = other.m_type;
            m_awaitable[READ] = other.m_awaitable[READ];
            m_awaitable[WRITE] = other.m_awaitable[WRITE];
            m_sequence_owner[READ] = other.m_sequence_owner[READ];
            m_sequence_owner[WRITE] = other.m_sequence_owner[WRITE];
            m_sequence_interest_mask = other.m_sequence_interest_mask;
            m_sequence_armed_mask = other.m_sequence_armed_mask;
#ifdef USE_EPOLL
            m_registered_events = other.m_registered_events;
#endif
#ifdef USE_IOURING
            clearSqeState();
            m_sqe_state[READ] = std::move(other.m_sqe_state[READ]);
            m_sqe_state[WRITE] = std::move(other.m_sqe_state[WRITE]);
            m_sqe_token_pool[READ] = std::move(other.m_sqe_token_pool[READ]);
            m_sqe_token_pool[WRITE] = std::move(other.m_sqe_token_pool[WRITE]);
            m_ready_accepts = std::move(other.m_ready_accepts);
            m_accept_multishot_token = other.m_accept_multishot_token;
            m_accept_multishot_armed = other.m_accept_multishot_armed;
            m_accept_result_assigned = other.m_accept_result_assigned;
            rebindSqeState();
#endif
            other.resetMovedFrom();
        }
        return *this;
    }

    /**
     * @brief 将 moved-from 对象重置到安全空状态
     * @note 供移动构造/赋值后清理源对象使用
     */
    void resetMovedFrom() noexcept {
        m_handle = GHandle::invalid();
        m_type = IOEventType::INVALID;
        m_awaitable[READ] = nullptr;
        m_awaitable[WRITE] = nullptr;
        m_sequence_owner[READ] = nullptr;
        m_sequence_owner[WRITE] = nullptr;
        m_sequence_interest_mask = 0;
        m_sequence_armed_mask = 0;
#ifdef USE_EPOLL
        m_registered_events = 0;
#endif
#ifdef USE_IOURING
        clearSqeState();
        m_sqe_state[READ] = std::make_shared<SqeState>(this, READ);
        m_sqe_state[WRITE] = std::make_shared<SqeState>(this, WRITE);
        m_sqe_token_pool[READ] = std::make_shared<SqeTokenArena>();
        m_sqe_token_pool[WRITE] = std::make_shared<SqeTokenArena>();
        m_ready_accepts.clear();
        m_accept_multishot_token = nullptr;
        m_accept_multishot_armed = false;
        m_accept_result_assigned = false;
#endif
    }

#ifdef USE_IOURING
    /**
     * @brief 为指定槽位生成 io_uring 提交令牌
     * @param slot READ 或 WRITE 槽位
     * @return 成功时返回池内稳定地址请求令牌；池缺失、状态缺失或扩容失败时返回 nullptr
     */
    SqeRequestToken* makeSqeRequest(Index slot) const {
        const auto& state = m_sqe_state[slot];
        const auto& arena = m_sqe_token_pool[slot];
        if (!state || !arena) {
            return nullptr;
        }
        return arena->acquire(state, arena);
    }

    /**
     * @brief 推进指定槽位的 generation
     * @param slot READ 或 WRITE 槽位
     * @note 在替换 awaitable 或重绑 owner 时调用，用于让旧 CQE 自动失效
     */
    void advanceSqeGeneration(Index slot) noexcept {
        if (auto& state = m_sqe_state[slot]; state) {
            state->generation.fetch_add(1, std::memory_order_acq_rel);
            state->owner.store(this, std::memory_order_release);
        }
    }

    /**
     * @brief 使当前控制器上所有历史 SQE 请求失效
     */
    void invalidateSqeRequests() noexcept {
        clearSqeState();
    }

    /**
     * @brief 将 accepted fd 缓存到 controller 侧队列
     * @param handle 新接受到的连接句柄
     */
    void enqueueAcceptedHandle(GHandle handle) {
        m_ready_accepts.push_back(handle);
    }

    /**
     * @brief 尝试从 ready queue 中取出一个 accept 结果
     * @param host 可选的输出 Host；为空时跳过地址解析
     * @param result 返回 accept 结果或错误
     * @return true 表示已消费一个缓存结果（成功或失败）；false 表示队列为空
     */
    bool tryConsumeAcceptedHandle(Host* host, std::expected<GHandle, IOError>& result) {
        if (m_ready_accepts.empty()) {
            return false;
        }

        GHandle handle = m_ready_accepts.front();
        m_ready_accepts.pop_front();

        if (host != nullptr) {
            sockaddr_storage addr{};
            socklen_t addr_len = sizeof(addr);
            if (::getpeername(handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
                result = std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno)));
                galay_close(handle.fd);
                return true;
            }
            *host = Host::fromSockAddr(addr);
        }

        result = handle;
        return true;
    }

    /**
     * @brief 清理 controller 内缓存但尚未交付的 accepted fd
     */
    void clearAcceptedHandles() noexcept {
        while (!m_ready_accepts.empty()) {
            const GHandle handle = m_ready_accepts.front();
            m_ready_accepts.pop_front();
            if (handle != GHandle::invalid()) {
                galay_close(handle.fd);
            }
        }
    }
#endif

    /**
     * @brief 填充Awaitable信息（支持 RECVWITHSEND 状态机）
     * @param type IO事件类型
     * @param awaitable 对应的Awaitable对象指针
     * @return true 填充成功；false 事件类型不受支持
     */
    bool fillAwaitable(IOEventType type, void* awaitable);

    /**
     * @brief 清除Awaitable信息（支持 RECVWITHSEND 状态机）
     * @param type IO事件类型
     */
    void removeAwaitable(IOEventType type);

    GHandle m_handle = GHandle::invalid();  ///< 关联的底层句柄
    IOEventType m_type = IOEventType::INVALID;  ///< 当前IO事件类型
    void* m_awaitable[IOController::SIZE] = {nullptr, nullptr};  ///< READ/WRITE 槽位上的 awaitable 指针
    SequenceAwaitableBase* m_sequence_owner[IOController::SIZE] = {nullptr, nullptr};  ///< READ/WRITE 槽位所属的 sequence awaitable
    uint8_t m_sequence_interest_mask = 0;  ///< sequence 关心的 READ/WRITE 位掩码
    uint8_t m_sequence_armed_mask = 0;  ///< 已经向 reactor 注册的 READ/WRITE 位掩码
#ifdef USE_EPOLL
    uint32_t m_registered_events = 0;          ///< epoll 已注册的事件掩码缓存
#endif
#ifdef USE_IOURING
    std::shared_ptr<SqeState> m_sqe_state[SIZE];  ///< io_uring READ/WRITE 槽位共享状态
    std::shared_ptr<SqeTokenArena> m_sqe_token_pool[SIZE];  ///< io_uring READ/WRITE 槽位令牌池
    std::deque<GHandle> m_ready_accepts;  ///< listener 缓存的 accepted fd，供下一次 accept() 直接消费
    SqeRequestToken* m_accept_multishot_token = nullptr;  ///< 当前 listener 持有的 multishot accept token
    bool m_accept_multishot_armed = false;  ///< listener 当前是否已挂上 multishot accept SQE
    bool m_accept_result_assigned = false;  ///< 当前 suspended accept awaitable 是否已写入一个结果
#endif

    /**
     * @brief 按具体 Awaitable 类型访问当前控制器中缓存的等待体
     * @tparam T 目标 awaitable 类型
     * @return 若当前槽位存在对应 awaitable，则返回其类型化指针；默认模板返回 nullptr
     * @note 具体事件类型的显式特化定义位于 IOScheduler.hpp
     */
    template<typename T>
    T* getAwaitable() { return nullptr; }

#ifdef USE_IOURING
private:
    void clearSqeState() noexcept {
        clearAcceptedHandles();
        m_accept_multishot_token = nullptr;
        m_accept_multishot_armed = false;
        m_accept_result_assigned = false;
        for (auto& state : m_sqe_state) {
            if (!state) {
                continue;
            }
            state->owner.store(nullptr, std::memory_order_release);
            state->generation.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void rebindSqeState() noexcept {
        for (auto& state : m_sqe_state) {
            if (!state) {
                continue;
            }
            state->owner.store(this, std::memory_order_release);
        }
    }
#endif
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_IOCONTROLLER_HPP
