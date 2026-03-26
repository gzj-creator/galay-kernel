#ifndef GALAY_KERNEL_DEFN_H
#define GALAY_KERNEL_DEFN_H


// Platform detection and configuration
#include <cstdint>

#if defined(__linux__)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>

    // Linux 后端必须由构建系统显式指定，避免库与下游编译单元出现宏不一致。
    #if defined(USE_EPOLL) && defined(USE_IOURING)
        #error "Both USE_EPOLL and USE_IOURING are defined. Select exactly one backend."
    #endif
    #if !defined(USE_EPOLL) && !defined(USE_IOURING)
        #error "No Linux backend macro defined. Build/link via galay-kernel CMake target, or pass -DUSE_EPOLL/-DUSE_IOURING explicitly."
    #endif

    // Linux-specific handle structure
    /**
     * @brief Linux 平台句柄包装
     * @details 当前仅封装文件描述符，`invalid()` 返回不可用句柄哨兵值。
     */
    struct GHandle {
        static GHandle invalid() { return GHandle{}; }  ///< 返回无效句柄
        int fd = -1;  ///< 底层文件描述符

        bool operator==(const GHandle& other) { return fd == other.fd; }  ///< 比较两个句柄是否指向同一 fd
        bool operator==(GHandle&& other) { return fd == other.fd; }  ///< 与右值句柄比较底层 fd
    };

    #include <sys/epoll.h>

#elif defined(__APPLE__)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #ifndef USE_KQUEUE
        #define USE_KQUEUE
    #endif

    // macOS-specific handle structure
    /**
     * @brief macOS/BSD 平台句柄包装
     * @details 当前仅封装文件描述符，`invalid()` 返回不可用句柄哨兵值。
     */
    struct GHandle {
        static GHandle invalid() { return GHandle{}; }  ///< 返回无效句柄
        int fd = -1;  ///< 底层文件描述符

        bool operator==(const GHandle& other) { return fd == other.fd; }  ///< 比较两个句柄是否指向同一 fd
        bool operator==(GHandle&& other) { return fd == other.fd; }  ///< 与右值句柄比较底层 fd
    };

    #include <sys/event.h>

#elif defined(WIN32) || defined(_WIN32) || defined(_WIN32_) || defined(WIN64) || defined(_WIN64) || defined(_WIN64_)
    #include <WinSock2.h>
    #pragma comment(lib,"ws2_32.lib")
    #define USE_IOCP
    #define close(x) closesocket(x)

    // Windows-specific handle structure
    /**
     * @brief Windows 平台句柄包装
     * @details 当前封装 socket 句柄，`invalid()` 返回 `INVALID_SOCKET`。
     */
    struct GHandle {
        static GHandle invalid() { return GHandle{INVALID_SOCKET}; }  ///< 返回无效句柄
        SOCKET fd = INVALID_SOCKET;  ///< 底层 socket 句柄

        bool operator==(const GHandle& other) { return fd == other.fd; }  ///< 比较两个句柄是否相等
        bool operator==(GHandle&& other) { return fd == other.fd; }  ///< 与右值句柄比较底层值
    };

    // Windows-specific type definitions
    typedef int socklen_t;
    typedef signed long ssize_t;

#else
    #error "Unsupported platform"
#endif

    /**
     * @brief IO 事件类型位掩码
     * @details 用于标识控制器当前关注的等待事件，可按位组合。
     */
    enum IOEventType: uint32_t {
        INVALID     = 0,       ///< 无效事件
        ACCEPT      = 1u << 0, ///< accept 等待
        CONNECT     = 1u << 1, ///< connect 等待
        RECV        = 1u << 2, ///< recv 等待
        SEND        = 1u << 3, ///< send 等待
        READV       = 1u << 4,   ///< scatter-gather 读取（readv）
        WRITEV      = 1u << 5,   ///< scatter-gather 写入（writev）
        SENDFILE    = 1u << 6,   ///< 零拷贝发送文件（sendfile）
        FILEREAD    = 1u << 7,   ///< 文件读取等待
        FILEWRITE   = 1u << 8,   ///< 文件写入等待
        FILEWATCH   = 1u << 9,   ///< 文件监控等待
        RECVFROM    = 1u << 10,  ///< recvfrom 等待
        SENDTO      = 1u << 11,  ///< sendto 等待
        SEQUENCE    = 1u << 12,  ///< 组合式序列 Awaitable
    };

    inline IOEventType operator|(IOEventType a, IOEventType b) {  ///< 合并两个事件位掩码
        return static_cast<IOEventType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline IOEventType operator&(IOEventType a, IOEventType b) {  ///< 计算两个事件位掩码的交集
        return static_cast<IOEventType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline IOEventType operator~(IOEventType a) {  ///< 按位取反事件掩码
        return static_cast<IOEventType>(~static_cast<uint32_t>(a));
    }
    inline IOEventType& operator|=(IOEventType& a, IOEventType b) {  ///< 就地合并事件位掩码
        a = a | b; return a;
    }
    inline IOEventType& operator&=(IOEventType& a, IOEventType b) {  ///< 就地计算事件位掩码交集
        a = a & b; return a;
    }

#endif
