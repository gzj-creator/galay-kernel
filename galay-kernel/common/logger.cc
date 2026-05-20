#include "galay-kernel/common/logger.h"

namespace
{
/**
 * @brief 持有 BaseLogger 所有权，确保程序退出时正确析构
 */
std::unique_ptr<galay::kernel::BaseLogger> g_owned;

/**
 * @brief 无锁读取的原子指针，指向 g_owned 管理的对象
 */
std::atomic<galay::kernel::BaseLogger*> g_instance{nullptr};

} // namespace

namespace galay::kernel
{

/**
 * @brief 设置全局 logger 并取得所有权
 *
 * @details 将用户提供的 logger 通过 unique_ptr 转移到内部 static 变量 g_owned 中，
 * 同时将裸指针以 release 语义写入原子变量 g_instance，确保后续所有线程的 get()
 * 都能看到完整的 logger 对象。
 *
 * @param logger 用户实现的日志实例，允许传入 nullptr 以禁用日志
 *
 * @note 线程不安全。不得与 get() 或其他 set() 并发调用。
 *       推荐在 main() 开头、创建任何 galay Runtime 之前调用一次。
 */
void LoggerRegistry::set(BaseLogger::uptr logger)
{
    g_owned = std::move(logger);
    g_instance.store(g_owned.get(), std::memory_order_release);
}

/**
 * @brief 获取当前全局 logger
 *
 * @details 通过 atomic acquire 语义读取 g_instance，保证看到 set() 中
 * store 的完整 BaseLogger 对象。未设置时返回 nullptr。
 *
 * @return 当前 logger 裸指针，或 nullptr
 *
 * @note 线程安全。可在任意线程、任意上下文中高频调用。
 * @note 返回的指针在 set() 被再次调用前保持有效。
 */
BaseLogger* LoggerRegistry::get() noexcept
{
    return g_instance.load(std::memory_order_acquire);
}

} // namespace galay::kernel
