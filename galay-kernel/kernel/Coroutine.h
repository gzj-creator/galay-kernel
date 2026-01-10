/**
 * @file Coroutine.h
 * @brief C++20 协程封装
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供基于C++20标准协程的封装，包括：
 * - Coroutine: 协程句柄包装类
 * - PromiseType: 协程Promise类型
 * - CoroutineData: 协程状态数据
 * - WaitResult: 协程等待结果
 *
 * @example
 * @code
 * // 定义协程函数
 * Coroutine myCoroutine() {
 *     // 异步操作
 *     co_await someAwaitable();
 *     co_return;
 * }
 *
 * // 提交到调度器执行
 * scheduler.spawn(myCoroutine());
 * @endcode
 */

#ifndef GALAY_KERNEL_COROUTINE_H
#define GALAY_KERNEL_COROUTINE_H

#include <coroutine>
#include <memory>
#include <optional>
#include <thread>

namespace galay::kernel
{

class PromiseType;
class CoroutineData;
class WaitResult;
class Waker;
class Scheduler;

/**
 * @brief 协程类
 *
 * @details 封装C++20协程句柄，提供协程生命周期管理。
 * 使用shared_ptr管理协程数据，支持拷贝和移动。
 *
 * @note
 * - 协程函数返回类型必须是Coroutine
 * - 协程内部使用co_await/co_return
 * - 通过scheduler.spawn()提交执行
 *
 * @see PromiseType, Scheduler
 */
class Coroutine
{
public:
    using promise_type = PromiseType;  ///< Promise类型别名，C++20协程要求

    friend class PromiseType;
    friend class Waker;
    friend class Scheduler;

    template <typename T>
    friend class MpscChannel;  // MpscChannel 需要访问 resume()

    /**
     * @brief 默认构造函数
     * @note 创建无效的协程对象
     */
    Coroutine() noexcept = default;

    /**
     * @brief 从协程句柄构造
     * @param handle C++20协程句柄
     */
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept;

    /**
     * @brief 移动构造函数
     * @param other 被移动的协程
     */
    Coroutine(Coroutine&& other) noexcept;

    /**
     * @brief 拷贝构造函数
     * @param other 被拷贝的协程
     * @note 使用shared_ptr，拷贝后共享同一协程数据
     */
    Coroutine(const Coroutine& other) noexcept;

    /**
     * @brief 移动赋值运算符
     * @param other 被移动的协程
     * @return 当前对象引用
     */
    Coroutine& operator=(Coroutine&& other) noexcept;

    /**
     * @brief 拷贝赋值运算符
     * @param other 被拷贝的协程
     * @return 当前对象引用
     */
    Coroutine& operator=(const Coroutine& other) noexcept;

    /**
     * @brief 检查协程是否有效
     * @return true 如果协程数据有效
     */
    bool isValid() const { return m_data != nullptr; }

    /**
     * @brief 设置后续协程
     * @param co 当前协程完成后要执行的协程
     * @return 当前协程引用，支持链式调用
     */
    Coroutine& then(Coroutine co);

    /**
     * @brief 等待协程完成
     * @return WaitResult 可等待对象
     * @note 在另一个协程中使用 co_await coro.wait()
     */
    WaitResult wait();

    /**
     * @brief 获取所属调度器
     * @return Scheduler* 调度器指针，可能为nullptr
     */
    Scheduler* belongScheduler() const;

    /**
     * @brief 设置所属调度器
     * @param scheduler 调度器指针
     */
    void belongScheduler(Scheduler* scheduler);

    /**
     * @brief 获取所属线程ID
     * @return 线程ID
     */
    std::thread::id threadId() const;

    /**
     * @brief 设置所属线程ID
     * @param id 线程ID
     */
    void threadId(std::thread::id id);

     /**
     * @brief 恢复协程执行
     * @note 仅供Waker和Scheduler调用，会在协程所属的scheduler上spawn
     */
     void resume();

private:
    std::shared_ptr<CoroutineData> m_data;  ///< 协程数据，使用shared_ptr管理生命周期
};

/**
 * @brief 协程等待结果
 *
 * @details 用于在一个协程中等待另一个协程完成。
 * 实现了Awaitable接口。
 *
 * @code
 * Coroutine outer() {
 *     Coroutine inner = someCoroutine();
 *     co_await inner.wait();  // 等待inner完成
 * }
 * @endcode
 */
struct WaitResult {
public:
    /**
     * @brief 构造函数
     * @param co 要等待的协程（拷贝以防止生命周期问题）
     */
    WaitResult(Coroutine co)
        : m_co(co) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false，需要挂起
     */
    bool await_ready() {
        return false;
    }

    /**
     * @brief 挂起当前协程
     * @param handle 当前协程句柄
     * @return 是否需要挂起
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时调用
     */
    void await_resume() {}

private:
    Coroutine m_co;  ///< 要等待的协程
};

/**
 * @brief 协程数据结构
 *
 * @details 存储协程的状态信息，使用64字节对齐避免伪共享。
 *
 * @note 由Coroutine通过shared_ptr管理
 */
struct alignas(64) CoroutineData
{
    CoroutineData() = default;

    ~CoroutineData() {
        // 协程完成后由 shared_ptr 最后一个引用释放时销毁句柄
        if (m_handle) {
            m_handle.destroy();
        }
    }

    std::coroutine_handle<Coroutine::promise_type> m_handle = nullptr;   ///< 底层协程句柄
    Scheduler* m_scheduler = nullptr;                                     ///< 所属调度器
    std::thread::id m_threadId;                                           ///< 所属线程ID
    std::optional<Coroutine> m_next;                                      ///< 后续协程（用于链式执行）
};

/**
 * @brief 协程Promise类型
 *
 * @details C++20协程要求的Promise类型，定义协程的行为。
 *
 * @note
 * - initial_suspend: 协程创建后立即挂起
 * - final_suspend: 协程完成后不挂起
 * - return_void: 支持co_return（无返回值）
 */
class PromiseType
{
public:
    using ReSchedulerType = bool;  ///< yield_value参数类型

    /**
     * @brief 分配失败时调用
     * @return 错误码
     */
    int get_return_object_on_alloaction_failure() noexcept { return -1; }

    /**
     * @brief 获取协程返回对象
     * @return Coroutine对象
     */
    Coroutine get_return_object() noexcept;

    /**
     * @brief 初始挂起点
     * @return suspend_always 协程创建后立即挂起
     */
    std::suspend_always initial_suspend() noexcept { return {}; }

    /**
     * @brief yield值处理
     * @param flag 是否重新调度
     * @return suspend_always 挂起协程
     */
    std::suspend_always yield_value(ReSchedulerType flag) noexcept;

    /**
     * @brief 最终挂起点
     * @return suspend_always 协程完成后保持挂起，由 CoroutineData 析构时销毁
     */
    std::suspend_always final_suspend() noexcept { return {}; }

    /**
     * @brief 未捕获异常处理
     */
    void unhandled_exception() noexcept {}

    /**
     * @brief 协程返回（无返回值）
     */
    void return_void() const noexcept;

    /**
     * @brief 获取关联的Coroutine对象
     * @return Coroutine对象
     */
    Coroutine getCoroutine() { return m_coroutine; }

    ~PromiseType() = default;

private:
    Coroutine m_coroutine;  ///< 关联的Coroutine对象
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_COROUTINE_H
