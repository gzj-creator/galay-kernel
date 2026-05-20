/**
 * @file logger.h
 * @brief 全局日志抽象接口与注册中心
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供可插拔的日志基础设施，galay 系列库（galay-http、galay-ssl 等）
 * 在关键路径上埋入日志点位，用户通过继承 BaseLogger 并调用 LoggerRegistry::set()
 * 注入自定义日志实现即可接收日志。未设置 logger 时，所有埋点仅执行一次 atomic
 * load + null 判断，不进入格式化，零开销。
 *
 * 使用方式：
 * @code
 * // 1. 用户实现 BaseLogger
 * class MyLogger : public galay::kernel::BaseLogger {
 * public:
 *     void log(LogLevel level, std::string_view tag,
 *              std::string_view message,
 *              const char* file, int line,
 *              const char* function) override {
 *         std::cout << std::format("[{}] {} {}:{} {}\n",
 *             levelToString(level), tag, file, line, message);
 *     }
 * };
 *
 * // 2. 在程序初始化时设置（仅调用一次，线程不安全）
 * galay::kernel::LoggerRegistry::set(
 *     std::make_unique<MyLogger>());
 *
 * // 3. 库内部埋点自动生效，无需其他操作
 * // 4. 若需重置，用户自行保证线程安全后再次调用 set()
 * @endcode
 */

#ifndef GALAY_KERNEL_LOGGER_H
#define GALAY_KERNEL_LOGGER_H

#include <atomic>
#include <memory>
#include <string_view>

namespace galay::kernel
{

/**
 * @brief 日志级别枚举
 *
 * @details 从低到高分为五个级别，用于过滤和分类日志消息。
 * LoggerRegistry::get()->minLevel() 返回的值决定了哪些级别的消息
 * 会被实际发送到日志实现。
 */
enum class LogLevel : uint8_t
{
    kTrace = 0, ///< 最详细的追踪信息，通常仅用于开发调试
    kDebug,     ///< 调试信息，用于排查问题时提供上下文
    kInfo,      ///< 一般信息，记录程序运行的关键事件
    kWarn,      ///< 警告信息，表示潜在问题但不影响程序运行
    kError,     ///< 错误信息，表示操作失败或异常情况
};

/**
 * @brief 抽象日志接口
 *
 * @details 用户继承此类并实现 log() 方法，即可将 galay 系列库内部的
 * 日志埋点重定向到任意日志后端（控制台、文件、远程日志服务等）。
 *
 * 生命周期约定：
 * - 实例通过 LoggerRegistry::set() 注入，由 LoggerRegistry 持有所有权
 * - 设置后在程序整个生命周期内保持有效
 * - 如需替换或销毁，用户须保证在没有任何库代码并发调用 get() 时执行
 *
 * 线程安全要求：
 * - log() 实现必须是线程安全的，因为多个 IO 线程可能并发写入日志
 * - minLevel() 实现应返回不变量（constexpr 或从 immutable 状态读取）
 */
class BaseLogger
{
public:
    using uptr = std::unique_ptr<BaseLogger>;

    /**
     * @brief 析构函数
     * @details 虚析构以保证通过基类指针销毁派生类时正确调用派生类析构。
     */
    virtual ~BaseLogger() = default;

    /**
     * @brief 核心日志写入方法
     *
     * @details 当日志宏（GALAY_LOG_* 等）检测到 logger 已设置且消息级别
     * 不低于 minLevel() 时，将调用此方法。调用方已完成消息格式化，
     * 实现只需将消息路由到目标后端。
     *
     * @param level    本次消息的日志级别
     * @param tag      埋点标签，标识消息来源，格式为 "[模块] [子模块] [事件]"，
     *                 例如 "[http] [connect] [host:port]"、"[ssl] [handshake] [fail]"
     * @param message  经 std::format 格式化后的日志正文
     * @param file     产生此日志的源文件路径（编译期常量，由 __builtin_FILE() 提供）
     * @param line     产生此日志的源文件行号（编译期常量，由 __builtin_LINE() 提供）
     * @param function 产生此日志的函数名（编译期常量，由 __builtin_FUNCTION() 提供）
     *
     * @note 实现必须是线程安全的。galay 系列库使用多线程 IO 模型，
     *       此方法可能从任意调度器线程并发调用。
     * @note 建议实现尽量减少阻塞时间，避免影响 IO 吞吐。
     *       如需写入文件或网络，建议使用异步队列缓冲。
     */
    virtual void log(LogLevel level,
                     std::string_view tag,
                     std::string_view message,
                     const char* file,
                     int line,
                     const char* function) = 0;

    /**
     * @brief 获取此 logger 接受的最低日志级别
     *
     * @details 日志宏在格式化消息之前会先检查此方法返回值，
     * 低于此级别的消息不会触发 log() 调用，也不会执行 std::format，
     * 从而实现零开销过滤。
     *
     * @return 最低日志级别，默认 kTrace（接收所有级别）
     *
     * @note 此方法应返回不变量。如果需要动态调整日志级别，
     *       返回值应使用 std::atomic 或由用户保证线程安全。
     */
    virtual LogLevel minLevel() const { return LogLevel::kTrace; }
};

/**
 * @brief 全局 Logger 注册中心
 *
 * @details 提供全局唯一的 logger 存储和访问接口。内部使用 atomic 裸指针
 * 实现 get() 的无锁访问，适合在高频 IO 路径中调用。
 *
 * 所有权模型：
 * - set() 接受 std::unique_ptr<BaseLogger>，取得 logger 的所有权
 * - 内部通过 static unique_ptr 持有，程序退出时自动析构
 *
 * 线程安全模型：
 * - get() 是线程安全的，使用 atomic acquire 语义，可在任意线程调用
 * - set() 是线程不安全的，用户须在程序初始化阶段（单线程环境）调用一次
 * - 若需在运行时替换 logger，用户须自行保证 set() 与 get() 的同步
 *
 * 使用示例：
 * @code
 * // 初始化时设置（main 函数开头）
 * galay::kernel::LoggerRegistry::set(std::make_unique<MyLogger>());
 *
 * // 库内部任意位置获取 logger 并检查
 * if (auto* logger = galay::kernel::LoggerRegistry::get()) {
 *     logger->log(LogLevel::kError, "[http] [connect]", "connection refused", ...);
 * }
 * @endcode
 */
class LoggerRegistry
{
public:
    /**
     * @brief 设置全局 logger
     *
     * @details 将 logger 存入全局原子指针并取得所有权。
     * 调用后所有线程的 get() 将返回新设置的 logger。
     *
     * @param logger 用户实现的日志实例，通过 unique_ptr 传入以转移所有权。
     *               传入 nullptr 等价于禁用日志（get() 返回 nullptr）。
     *
     * @note 线程不安全。不得与 get() 并发调用。
     *       推荐在 main() 开头、创建任何 galay Runtime 之前调用。
     *
     * @code
     * galay::kernel::LoggerRegistry::set(std::make_unique<MyLogger>());
     * @endcode
     */
    static void set(BaseLogger::uptr logger);

    /**
     * @brief 获取当前全局 logger
     *
     * @details 返回通过 set() 设置的 logger 裸指针，未设置时返回 nullptr。
     * 使用 atomic acquire 语义，保证看到 set() 中 store 的完整 logger 对象。
     *
     * @return 当前 logger 指针，或 nullptr（未设置时）
     *
     * @note 线程安全。可在任意线程、任意上下文中调用。
     * @note 返回的指针在 set() 被再次调用或程序退出前保持有效。
     */
    [[nodiscard]] static BaseLogger* get() noexcept;

    /**
     * @brief 禁止实例化
     * @details LoggerRegistry 仅提供静态方法，不允许创建实例。
     */
    LoggerRegistry() = delete;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_LOGGER_H
