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
    };

    #include <sys/epoll.h>

#elif defined(__APPLE__)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define USE_KQUEUE

    // macOS-specific handle structure
    struct GHandle {
        static GHandle invalid() { return GHandle{}; }
        int fd = -1;
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
    };

    // Windows-specific type definitions
    typedef int socklen_t;
    typedef signed long ssize_t;

#else
    #error "Unsupported platform"
#endif

    enum IOEventType: uint32_t {
        INVALID         = 0,
        ACCEPT          = 1,
        CONNECT         = 2,
        RECV            = 3,
        SEND            = 4,
        RECVWITHSEND    = 5,
        FILEREAD        = 6,
        FILEWRITE       = 7,
        FILEWATCH       = 8,
        RECVFROM        = 9,
        SENDTO          = 10,
        SLEEP           = 11,
    };

#endif