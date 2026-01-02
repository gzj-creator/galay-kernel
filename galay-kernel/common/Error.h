#ifndef GALAY_KERNEL_ERROR_H
#define GALAY_KERNEL_ERROR_H

#include <cstdint>
#include <string>

namespace galay::kernel
{

// Error codes
enum IOErrorCode : uint32_t {
    kDisconnectError = 0,
    kNotifyButSourceNotReady = 1,
    kRecvFailed = 2,
    kSendFailed = 3,
    kAcceptFailed = 4,
    kConnectFailed = 5,
    kBindFailed = 6,
    kListenFailed = 7,
    kOpenFailed = 8,
    kReadFailed = 9,
    kWriteFailed = 10,
    kStatFailed = 11,
    kSyncFailed = 12,
    kSeekFailed = 13,
    kTimeout = 14,              ///< 操作超时
};

/**
* @brief 通用错误类
* @details 封装IO错误码和系统错误码，提供错误信息查询功能
*/
class IOError
{
public:
    /**
    * @brief 检查错误码是否包含指定错误类型
    * @param error 错误码
    * @param code 要检查的错误类型
    * @return 是否包含该错误类型
    */
    static bool contains(uint64_t error, IOErrorCode code);

    /**
    * @brief 构造错误对象
    * @param io_error_code IO错误码
    * @param system_code 系统错误码（如errno）
    */
    IOError(IOErrorCode io_error_code, uint32_t system_code);

    /**
    * @brief 获取组合后的错误码
    * @return 64位错误码（高32位为系统码，低32位为IO错误码）
    */
    uint64_t code() const;

    /**
    * @brief 获取错误消息字符串
    * @return 可读的错误描述
    */
    std::string message() const;

    /**
    * @brief 重置错误为无错误状态
    */
    void reset();
private:
    /**
    * @brief 将两个32位错误码组合成64位
    * @param io_error_code IO错误码
    * @param system_code 系统错误码
    */
    uint64_t makeErrorCode(IOErrorCode io_error_code, uint32_t system_code);
private:
    uint64_t m_code;  ///< 组合后的错误码
};

/**
* @brief 无错误类型
* @details 用于表示不会失败的操作
*/
class Infallible
{
};

}

#endif
