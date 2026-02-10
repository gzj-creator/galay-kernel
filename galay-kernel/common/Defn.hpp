#ifndef GALAY_KERNEL_DEFN_H
#define GALAY_KERNEL_DEFN_H


// Platform detection and configuration
#include <cstdint>

#if defined(__linux__)
    #include <linux/version.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>

    // Choose I/O multiplexing mechanism based on kernel version
    // 如果 CMake 已经定义了 USE_EPOLL 或 USE_IOURING，则不再自动检测
    #if !defined(USE_EPOLL) && !defined(USE_IOURING)
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
            #define USE_IOURING
        #else
            #define USE_EPOLL
        #endif
    #endif

    // Linux-specific handle structure
    struct GHandle {
        static GHandle invalid() { return GHandle{}; }
        int fd = -1;

        bool operator==(const GHandle& other) { return fd == other.fd; }
        bool operator==(GHandle&& other) { return fd == other.fd; }
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
    struct GHandle {
        static GHandle invalid() { return GHandle{}; }
        int fd = -1;

        bool operator==(const GHandle& other) { return fd == other.fd; }
        bool operator==(GHandle&& other) { return fd == other.fd; }
    };

    #include <sys/event.h>

#elif defined(WIN32) || defined(_WIN32) || defined(_WIN32_) || defined(WIN64) || defined(_WIN64) || defined(_WIN64_)
    #include <WinSock2.h>
    #pragma comment(lib,"ws2_32.lib")
    #define USE_IOCP
    #define close(x) closesocket(x)

    // Windows-specific handle structure
    struct GHandle {
        static GHandle invalid() { return GHandle{INVALID_SOCKET}; }
        SOCKET fd = INVALID_SOCKET;

        bool operator==(const GHandle& other) { return fd == other.fd; }
        bool operator==(GHandle&& other) { return fd == other.fd; }
    };

    // Windows-specific type definitions
    typedef int socklen_t;
    typedef signed long ssize_t;

#else
    #error "Unsupported platform"
#endif

    enum IOEventType: uint32_t {
        INVALID     = 0,
        ACCEPT      = 1u << 0,
        CONNECT     = 1u << 1,
        RECV        = 1u << 2,
        SEND        = 1u << 3,
        READV       = 1u << 4,   ///< scatter-gather 读取（readv）
        WRITEV      = 1u << 5,   ///< scatter-gather 写入（writev）
        SENDFILE    = 1u << 6,   ///< 零拷贝发送文件（sendfile）
        FILEREAD    = 1u << 7,
        FILEWRITE   = 1u << 8,
        FILEWATCH   = 1u << 9,
        RECVFROM    = 1u << 10,
        SENDTO      = 1u << 11,
        RECV_NOTIFY = 1u << 12,  ///< 仅通知可读，不执行IO操作（用于SSL等自定义IO）
        SEND_NOTIFY = 1u << 13,  ///< 仅通知可写，不执行IO操作（用于SSL等自定义IO）
        CUSTOM      = 1u << 14,  ///< 用户自定义
    };

    inline IOEventType operator|(IOEventType a, IOEventType b) {
        return static_cast<IOEventType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline IOEventType operator&(IOEventType a, IOEventType b) {
        return static_cast<IOEventType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline IOEventType operator~(IOEventType a) {
        return static_cast<IOEventType>(~static_cast<uint32_t>(a));
    }
    inline IOEventType& operator|=(IOEventType& a, IOEventType b) {
        a = a | b; return a;
    }
    inline IOEventType& operator&=(IOEventType& a, IOEventType b) {
        a = a & b; return a;
    }

#endif