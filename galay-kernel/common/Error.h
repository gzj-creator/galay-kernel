#ifndef GALAY_KERNEL_ERROR_H
#define GALAY_KERNEL_ERROR_H

#include <cstdint>
#include <string>

namespace galay::kernel
{

/**
 * @brief galay-kernel 统一 IO 错误码
 */
enum IOErrorCode : uint32_t {
    kDisconnectError = 0,  ///< 连接已断开或关闭失败
    kNotReady,             ///< 资源尚未准备好或当前状态不允许该操作
    kParamInvalid,         ///< 调用参数无效
    kRecvFailed,           ///< 接收数据失败
    kSendFailed,           ///< 发送数据失败
    kAcceptFailed,         ///< 接收新连接失败
    kConnectFailed,        ///< 发起连接失败
    kBindFailed,           ///< 绑定地址失败
    kListenFailed,         ///< 开始监听失败
    kOpenFailed,           ///< 打开文件失败
    kReadFailed,           ///< 读取失败
    kWriteFailed,          ///< 写入失败
    kStatFailed,           ///< 查询文件状态失败
    kSyncFailed,           ///< 同步文件到磁盘失败
    kSeekFailed,           ///< 调整文件偏移失败
    kTimeout,              ///< 操作超时
    kNotRunningOnIOScheduler  ///< 当前执行上下文不在 IO scheduler 上
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
