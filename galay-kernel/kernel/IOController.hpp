#ifndef GALAY_KERNEL_IOCONTROLLER_HPP
#define GALAY_KERNEL_IOCONTROLLER_HPP

#include "galay-kernel/common/Defn.hpp"

#ifdef USE_IOURING
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
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
struct SqeRequestToken {
    std::shared_ptr<SqeState> state;  ///< SQE 共享状态
    uint64_t generation;  ///< 本次提交时观测到的 generation
};
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

    /**
     * @brief 复制构造 IO 控制器
     * @param other 被复制的控制器
     * @note awaitable 指针和序列状态会被浅拷贝；io_uring 状态会重新绑定到新对象
     */
    IOController(const IOController& other) noexcept
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
        , m_sqe_state{
            std::make_shared<SqeState>(this, READ),
            std::make_shared<SqeState>(this, WRITE)}
#endif
    {}

    /**
     * @brief 复制赋值 IO 控制器
     * @param other 被复制的控制器
     * @return 当前对象引用
     * @note awaitable 指针和序列状态会被浅拷贝；io_uring 状态会重新绑定到当前对象
     */
    IOController& operator=(const IOController& other) noexcept {
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
            m_sqe_state[READ] = std::make_shared<SqeState>(this, READ);
            m_sqe_state[WRITE] = std::make_shared<SqeState>(this, WRITE);
#endif
        }
        return *this;
    }

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
#endif
    }

#ifdef USE_IOURING
    /**
     * @brief 为指定槽位生成 io_uring 提交令牌
     * @param slot READ 或 WRITE 槽位
     * @return 成功时返回新分配的请求令牌；分配失败或状态缺失时返回 nullptr
     */
    SqeRequestToken* makeSqeRequest(Index slot) const {
        const auto& state = m_sqe_state[slot];
        if (!state) {
            return nullptr;
        }
        return new (std::nothrow) SqeRequestToken{
            .state = state,
            .generation = state->generation.load(std::memory_order_acquire)};
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
