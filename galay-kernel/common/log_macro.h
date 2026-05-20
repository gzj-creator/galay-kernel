/**
 * @file log_macro.h
 * @brief galay 系列库通用日志宏
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供零开销的日志埋点宏，供 galay-http、galay-ssl 等下游库使用。
 * 宏在展开时先通过 LoggerRegistry::get() 获取 logger 裸指针（atomic load），
 * 再检查 minLevel()，两者均通过后才执行 std::format 并调用 log()。
 * 未设置 logger 时，仅执行一次 atomic load + null check，不进入格式化。
 *
 * 各下游库应基于此文件定义自己的命名空间宏（如 HTTP_LOG_*、SSL_LOG_*），
 * 在 tag 前缀中加上模块标识，例如 "[http] [connect]"、"[ssl] [handshake]"。
 *
 * 使用示例：
 * @code
 * // 在 galay-http 中定义
 * #define HTTP_LOG_ERROR(tag, ...) \
 *     GALAY_LOG_ERROR("[http] " tag, __VA_ARGS__)
 *
 * // 在库代码中埋点
 * HTTP_LOG_ERROR("[connect] [fail]", "host={}:{} error={}", host, port, err);
 * @endcode
 */

#ifndef GALAY_KERNEL_LOG_MACRO_H
#define GALAY_KERNEL_LOG_MACRO_H

#include "galay-kernel/common/logger.h"

#include <format>
#include <string>

/**
 * @brief 通用日志宏（核心实现）
 *
 * @details 检查 logger 是否设置且消息级别不低于 minLevel，
 * 通过后使用 std::format 格式化消息并调用 log()。
 * 使用 __builtin_FILE()/__builtin_LINE()/__builtin_FUNCTION()
 * 捕获调用点的源代码位置。
 *
 * @param level LogLevel 枚举值
 * @param tag   埋点标签字符串字面量，如 "[http] [connect]"
 * @param fmt   std::format 兼容的格式字符串
 * @param ...   格式化参数（可变）
 *
 * @note 零开销保证：logger 为 nullptr 时，std::format 不会被执行。
 * @note 变量名使用 _galay_ 前缀避免与调用方作用域冲突。
 */
#define GALAY_LOG(level, tag, fmt, ...)                                          \
    do {                                                                         \
        auto* const _galay_log_ptr = ::galay::kernel::LoggerRegistry::get();     \
        if (_galay_log_ptr && _galay_log_ptr->minLevel() <= (level)) {           \
            std::string _galay_log_msg =                                         \
                std::format(fmt __VA_OPT__(,) __VA_ARGS__);                      \
            _galay_log_ptr->log(level, tag, _galay_log_msg,                      \
                                 __builtin_FILE(), __builtin_LINE(),             \
                                 __builtin_FUNCTION());                          \
        }                                                                        \
    } while (0)

/// @brief 追踪级别日志宏，用于最详细的开发调试信息
#define GALAY_LOG_TRACE(tag, ...) \
    GALAY_LOG(::galay::kernel::LogLevel::kTrace, tag, __VA_ARGS__)

/// @brief 调试级别日志宏，用于排查问题时的上下文信息
#define GALAY_LOG_DEBUG(tag, ...) \
    GALAY_LOG(::galay::kernel::LogLevel::kDebug, tag, __VA_ARGS__)

/// @brief 信息级别日志宏，用于记录程序运行的关键事件
#define GALAY_LOG_INFO(tag, ...) \
    GALAY_LOG(::galay::kernel::LogLevel::kInfo, tag, __VA_ARGS__)

/// @brief 警告级别日志宏，用于表示潜在问题
#define GALAY_LOG_WARN(tag, ...) \
    GALAY_LOG(::galay::kernel::LogLevel::kWarn, tag, __VA_ARGS__)

/// @brief 错误级别日志宏，用于表示操作失败或异常情况
#define GALAY_LOG_ERROR(tag, ...) \
    GALAY_LOG(::galay::kernel::LogLevel::kError, tag, __VA_ARGS__)

#endif // GALAY_KERNEL_LOG_MACRO_H
