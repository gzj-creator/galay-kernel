/**
 * @file Log.h
 * @brief 日志系统封装
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 基于spdlog封装的全局日志系统，支持：
 * - 编译期开关：通过USE_LOG宏控制
 * - 运行时开关：Logger::enable()/disable()
 * - 多级别日志：trace/debug/info/warn/error/critical
 * - 自动文件名和行号
 *
 * @note 需要在CMake中设置ENABLE_LOG=ON才能启用日志功能
 *
 * @example
 * @code
 * // 使用日志宏
 * LogInfo("Server started on port {}", 8080);
 * LogError("Connection failed: {}", error.message());
 *
 * // 运行时控制
 * Logger::disable();  // 禁用日志
 * Logger::enable();   // 启用日志
 *
 * // 设置日志级别
 * Logger::setLevel(spdlog::level::warn);  // 只显示warn及以上
 * @endcode
 */

#ifndef GALAY_KERNEL_LOG_H
#define GALAY_KERNEL_LOG_H

#ifdef USE_LOG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <atomic>

namespace galay::kernel
{

/**
 * @brief 全局日志器类（单例模式）
 *
 * @details 封装spdlog，提供线程安全的日志功能。
 * 日志格式：[时间] [级别] [文件:行号] 消息
 *
 * @note
 * - 使用单例模式，全局唯一实例
 * - 线程安全，可在多线程环境使用
 * - 建议使用LogXxx宏而非直接调用方法
 */
class Logger
{
public:
    /**
     * @brief 获取单例实例
     * @return Logger& 日志器单例引用
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    /**
     * @brief 启用日志输出
     * @note 线程安全
     */
    static void enable() {
        instance().m_enabled.store(true, std::memory_order_release);
    }

    /**
     * @brief 禁用日志输出
     * @note 线程安全，禁用后所有日志调用将被忽略
     */
    static void disable() {
        instance().m_enabled.store(false, std::memory_order_release);
    }

    /**
     * @brief 检查日志是否启用
     * @return true 日志已启用
     * @return false 日志已禁用
     */
    static bool isEnabled() {
        return instance().m_enabled.load(std::memory_order_acquire);
    }

    /**
     * @brief 设置日志级别
     * @param level spdlog日志级别（trace/debug/info/warn/err/critical/off）
     *
     * @code
     * Logger::setLevel(spdlog::level::info);  // 只显示info及以上级别
     * @endcode
     */
    static void setLevel(spdlog::level::level_enum level) {
        instance().m_logger->set_level(level);
    }

    /**
     * @brief 获取底层spdlog日志器
     * @return std::shared_ptr<spdlog::logger>& spdlog日志器引用
     * @note 用于高级操作，一般不需要直接使用
     */
    static std::shared_ptr<spdlog::logger>& logger() {
        return instance().m_logger;
    }

    /**
     * @brief 输出trace级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->trace(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 输出debug级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->debug(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 输出info级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->info(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 输出warn级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->warn(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 输出error级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->error(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 输出critical级别日志
     * @tparam Args 格式化参数类型
     * @param fmt 格式字符串
     * @param args 格式化参数
     */
    template<typename... Args>
    static void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        if (isEnabled()) {
            instance().m_logger->critical(fmt, std::forward<Args>(args)...);
        }
    }

private:
    /**
     * @brief 私有构造函数
     * @details 初始化spdlog日志器，设置输出格式
     */
    Logger() {
        m_logger = spdlog::stdout_color_mt("galay");
        // 格式: [时间] [级别] [文件:行号] 消息
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        m_logger->set_level(spdlog::level::trace);
        m_enabled.store(true, std::memory_order_release);
    }

    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::shared_ptr<spdlog::logger> m_logger;  ///< spdlog日志器实例
    std::atomic<bool> m_enabled{true};         ///< 日志启用标志
};

} // namespace galay::kernel

/**
 * @def LogTrace
 * @brief 输出trace级别日志（最详细）
 * @param ... 格式字符串和参数
 * @note 自动添加文件名和行号
 */
#define LogTrace(...)    do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::trace, __VA_ARGS__); } while(0)

/**
 * @def LogDebug
 * @brief 输出debug级别日志
 * @param ... 格式字符串和参数
 */
#define LogDebug(...)    do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::debug, __VA_ARGS__); } while(0)

/**
 * @def LogInfo
 * @brief 输出info级别日志
 * @param ... 格式字符串和参数
 */
#define LogInfo(...)     do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::info, __VA_ARGS__); } while(0)

/**
 * @def LogWarn
 * @brief 输出warn级别日志
 * @param ... 格式字符串和参数
 */
#define LogWarn(...)     do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::warn, __VA_ARGS__); } while(0)

/**
 * @def LogError
 * @brief 输出error级别日志
 * @param ... 格式字符串和参数
 */
#define LogError(...)    do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::err, __VA_ARGS__); } while(0)

/**
 * @def LogCritical
 * @brief 输出critical级别日志（最严重）
 * @param ... 格式字符串和参数
 */
#define LogCritical(...) do { if (galay::kernel::Logger::isEnabled()) SPDLOG_LOGGER_CALL(galay::kernel::Logger::logger(), spdlog::level::critical, __VA_ARGS__); } while(0)

#else // USE_LOG not defined

/**
 * @brief 空日志宏（USE_LOG未定义时）
 * @details 当USE_LOG未定义时，所有日志宏展开为空操作，零开销
 */
#define LogTrace(...)    ((void)0)
#define LogDebug(...)    ((void)0)
#define LogInfo(...)     ((void)0)
#define LogWarn(...)     ((void)0)
#define LogError(...)    ((void)0)
#define LogCritical(...) ((void)0)

namespace galay::kernel
{

/**
 * @brief 空日志器类（USE_LOG未定义时）
 * @details 提供空实现，保持API兼容性
 */
class Logger
{
public:
    static void enable() {}
    static void disable() {}
    static bool isEnabled() { return false; }
};

} // namespace galay::kernel

#endif // USE_LOG

#endif // GALAY_KERNEL_LOG_H
