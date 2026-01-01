# Galay-Kernel API 文档

## 目录

- [核心概念](#核心概念)
- [调度器](#调度器)
- [协程](#协程)
- [网络 IO](#网络-io)
- [文件 IO](#文件-io)
- [错误处理](#错误处理)
- [工具类](#工具类)

---

## 核心概念

### 事件驱动架构

Galay-Kernel 采用事件驱动的异步 IO 模型：

```
┌─────────────────────────────────────────────────────────┐
│                      应用层                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │  TcpSocket  │  │  AsyncFile  │  │  Coroutine  │     │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘     │
│         │                │                │             │
│  ┌──────┴────────────────┴────────────────┴──────┐     │
│  │              IOScheduler (抽象层)              │     │
│  └──────────────────────┬────────────────────────┘     │
│                         │                               │
├─────────────────────────┼───────────────────────────────┤
│                         │           平台层              │
│  ┌──────────────────────┼──────────────────────┐       │
│  │  macOS: KqueueScheduler                      │       │
│  │  Linux: EpollScheduler / IOUringScheduler    │       │
│  └──────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────┘
```

### 协程工作流程

```
1. 创建协程函数 (返回 Coroutine)
2. 调用 scheduler.spawn(coroutine) 提交到调度器
3. 协程执行到 co_await 时挂起
4. IO 事件完成后，调度器唤醒协程继续执行
5. 协程执行完毕或遇到 co_return 结束
```

---

## 调度器

### IOScheduler (抽象基类)

所有平台调度器的基类接口。

```cpp
namespace galay::kernel {

class IOScheduler : public Scheduler {
public:
    // 注册 IO 事件
    virtual int addAccept(IOController* controller) = 0;
    virtual int addConnect(IOController* controller) = 0;
    virtual int addRecv(IOController* controller) = 0;
    virtual int addSend(IOController* controller) = 0;
    virtual int addClose(int fd) = 0;
    virtual int addFileRead(IOController* controller) = 0;
    virtual int addFileWrite(IOController* controller) = 0;

    // 提交协程
    virtual void spawn(Coroutine coro) = 0;
};

}
```

### KqueueScheduler (macOS)

基于 kqueue 的调度器实现。

```cpp
namespace galay::kernel {

class KqueueScheduler : public IOScheduler {
public:
    // 构造函数
    KqueueScheduler(
        int max_events = 1024,           // 单次处理最大事件数
        int batch_size = 256,            // 协程批处理大小
        int check_interval_ms = 1        // 事件检查间隔 (毫秒)
    );

    // 生命周期
    void start();   // 启动调度器线程
    void stop();    // 停止调度器
    void notify();  // 唤醒调度器

    // 提交协程
    void spawn(Coroutine coro) override;
};

}
```

**使用示例：**

```cpp
#include "galay-kernel/kernel/KqueueScheduler.h"

int main() {
    KqueueScheduler scheduler;
    scheduler.start();

    scheduler.spawn(myCoroutine(&scheduler));

    // 等待信号或其他退出条件
    std::this_thread::sleep_for(std::chrono::seconds(60));

    scheduler.stop();
    return 0;
}
```

### EpollScheduler (Linux)

基于 epoll + libaio 的调度器实现。

```cpp
namespace galay::kernel {

class EpollScheduler : public IOScheduler {
public:
    EpollScheduler(
        int max_events = 1024,
        int batch_size = 256,
        int check_interval_ms = 1
    );

    void start();
    void stop();
    void notify();
    void spawn(Coroutine coro) override;
};

}
```

### IOUringScheduler (Linux 5.1+)

基于 io_uring 的高性能调度器实现。

```cpp
namespace galay::kernel {

class IOUringScheduler : public IOScheduler {
public:
    IOUringScheduler(
        int queue_depth = 4096,   // io_uring 队列深度
        int batch_size = 256      // 协程批处理大小
    );

    void start();
    void stop();
    void notify();
    void spawn(Coroutine coro) override;
};

}
```

**io_uring 特性：**
- 原生支持网络和文件 IO
- 零系统调用开销 (SQPOLL 模式)
- 批量提交和完成

---

## 协程

### Coroutine

协程句柄包装类。

```cpp
namespace galay::kernel {

class Coroutine {
public:
    using promise_type = PromiseType;

    // 构造
    Coroutine() noexcept = default;
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept;

    // 拷贝和移动
    Coroutine(const Coroutine& other) noexcept;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other) noexcept;
    Coroutine& operator=(Coroutine&& other) noexcept;

    // 状态查询
    bool isRunning() const;   // 是否正在运行
    bool isSuspend() const;   // 是否已挂起
    bool isDone() const;      // 是否已完成

    // 链式执行
    Coroutine& then(Coroutine co);  // 设置后续协程

    // 等待完成
    WaitResult wait();  // 返回可等待对象
};

}
```

**定义协程函数：**

```cpp
Coroutine myCoroutine(IOScheduler* scheduler) {
    // 协程体
    co_await someAwaitable();
    co_return;
}
```

**链式执行：**

```cpp
Coroutine first(IOScheduler* scheduler) {
    // 第一个任务
    co_return;
}

Coroutine second(IOScheduler* scheduler) {
    // 第二个任务
    co_return;
}

// first 完成后自动执行 second
scheduler->spawn(first(scheduler).then(second(scheduler)));
```

**等待其他协程：**

```cpp
Coroutine outer(IOScheduler* scheduler) {
    Coroutine inner = someCoroutine(scheduler);
    scheduler->spawn(inner);

    co_await inner.wait();  // 等待 inner 完成
    // 继续执行
}
```

### Waker

协程唤醒器，用于在 IO 完成时恢复协程。

```cpp
namespace galay::kernel {

class Waker {
public:
    Waker();
    Waker(std::coroutine_handle<> handle);

    void wakeUp();  // 唤醒关联的协程
};

}
```

---

## 网络 IO

### TcpSocket

异步 TCP Socket 封装。

```cpp
namespace galay::async {

class TcpSocket {
public:
    // 构造
    explicit TcpSocket(IOScheduler* scheduler);
    TcpSocket(IOScheduler* scheduler, GHandle handle);

    // 禁止拷贝，允许移动
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;

    // 属性
    GHandle handle() const;
    IOController* controller();
    bool isValid() const;

    // 同步操作
    std::expected<void, IOError> create(IPType type = IPType::IPV4);
    std::expected<void, IOError> bind(const Host& host);
    std::expected<void, IOError> listen(int backlog = 128);
    HandleOption option();

    // 异步操作
    AcceptAwaitable accept(Host* clientHost);
    ConnectAwaitable connect(const Host& host);
    RecvAwaitable recv(char* buffer, size_t length);
    SendAwaitable send(const char* buffer, size_t length);
    CloseAwaitable close();
};

}
```

**服务端示例：**

```cpp
Coroutine echoServer(IOScheduler* scheduler) {
    TcpSocket listener(scheduler);

    // 创建并配置 socket
    listener.create(IPType::IPV4);
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    // 绑定并监听
    listener.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
    listener.listen(1024);

    while (true) {
        Host clientHost;
        auto result = co_await listener.accept(&clientHost);

        if (result) {
            // 为每个客户端创建处理协程
            scheduler->spawn(handleClient(scheduler, result.value()));
        }
    }
}

Coroutine handleClient(IOScheduler* scheduler, GHandle handle) {
    TcpSocket client(scheduler, handle);
    client.option().handleNonBlock();

    char buffer[4096];
    while (true) {
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));

        if (!recvResult) {
            // 错误处理
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            // 对端关闭连接
            break;
        }

        // Echo 回去
        co_await client.send(bytes.c_str(), bytes.size());
    }

    co_await client.close();
}
```

**客户端示例：**

```cpp
Coroutine echoClient(IOScheduler* scheduler) {
    TcpSocket socket(scheduler);

    socket.create(IPType::IPV4);
    socket.option().handleNonBlock();

    // 连接服务器
    auto connectResult = co_await socket.connect(Host(IPType::IPV4, "127.0.0.1", 8080));
    if (!connectResult) {
        // 连接失败
        co_return;
    }

    // 发送数据
    const char* msg = "Hello, Server!";
    co_await socket.send(msg, strlen(msg));

    // 接收响应
    char buffer[1024];
    auto recvResult = co_await socket.recv(buffer, sizeof(buffer));

    if (recvResult) {
        // 处理响应
    }

    co_await socket.close();
}
```

### Host

网络地址封装，支持 IPv4 和 IPv6。

```cpp
namespace galay::kernel {

enum class IPType : uint8_t {
    IPV4 = 0,
    IPV6 = 1,
};

struct Host {
    // 构造
    Host();  // 默认 IPv4
    Host(IPType proto, const std::string& ip, uint16_t port);
    Host(const sockaddr_in& addr);   // 从 IPv4 地址构造
    Host(const sockaddr_in6& addr);  // 从 IPv6 地址构造

    // 静态工厂
    static Host fromSockAddr(const sockaddr_storage& addr);

    // 属性
    bool isIPv4() const;
    bool isIPv6() const;
    std::string ip() const;
    uint16_t port() const;

    // 底层访问
    sockaddr* sockAddr();
    const sockaddr* sockAddr() const;
    socklen_t* addrLen();
    socklen_t addrLen() const;
};

}
```

### HandleOption

Socket 选项配置器。

```cpp
namespace galay::kernel {

class HandleOption {
public:
    HandleOption(GHandle handle);

    // 阻塞模式
    std::expected<void, IOError> handleBlock();
    std::expected<void, IOError> handleNonBlock();

    // 地址重用
    std::expected<void, IOError> handleReuseAddr();
    std::expected<void, IOError> handleReusePort();

    // TCP 选项
    std::expected<void, IOError> handleNoDelay();      // TCP_NODELAY
    std::expected<void, IOError> handleKeepAlive();    // SO_KEEPALIVE
};

}
```

### UdpSocket

异步 UDP Socket 封装。

```cpp
namespace galay::async {

class UdpSocket {
public:
    // 构造
    explicit UdpSocket(IOScheduler* scheduler);
    UdpSocket(IOScheduler* scheduler, GHandle handle);

    // 禁止拷贝，允许移动
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;

    // 属性
    GHandle handle() const;
    IOController* controller();
    bool isValid() const;

    // 同步操作
    std::expected<void, IOError> create(IPType type = IPType::IPV4);
    std::expected<void, IOError> bind(const Host& host);
    HandleOption option();

    // 异步操作
    RecvFromAwaitable recvfrom(char* buffer, size_t length, Host* from);
    SendToAwaitable sendto(const char* buffer, size_t length, const Host& to);
    CloseAwaitable close();
};

}
```

**UDP 特性：**
- 无连接协议，不需要 `listen()`、`accept()`、`connect()`
- `recvfrom()` 可以获取发送方地址
- `sendto()` 可以指定目标地址
- 数据报协议，每次收发一个完整的数据报
- 不保证数据送达和顺序

**服务端示例：**

```cpp
Coroutine udpEchoServer(IOScheduler* scheduler) {
    UdpSocket socket(scheduler);

    // 创建并配置 socket
    socket.create(IPType::IPV4);
    socket.option().handleReuseAddr();
    socket.option().handleNonBlock();

    // 绑定端口
    socket.bind(Host(IPType::IPV4, "0.0.0.0", 8080));

    char buffer[65536];  // UDP 最大数据报大小
    while (true) {
        Host from;
        auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);

        if (!recvResult) {
            // 错误处理
            continue;
        }

        auto& bytes = recvResult.value();

        // Echo 回发送方
        co_await socket.sendto(bytes.c_str(), bytes.size(), from);
    }

    co_await socket.close();
}
```

**客户端示例：**

```cpp
Coroutine udpClient(IOScheduler* scheduler) {
    UdpSocket socket(scheduler);

    socket.create(IPType::IPV4);
    socket.option().handleNonBlock();

    // UDP 客户端通常不需要 bind，系统会自动分配端口

    Host server(IPType::IPV4, "127.0.0.1", 8080);

    // 发送数据
    const char* msg = "Hello, UDP Server!";
    auto sendResult = co_await socket.sendto(msg, strlen(msg), server);

    if (!sendResult) {
        // 发送失败
        co_return;
    }

    // 接收响应
    char buffer[1024];
    Host from;
    auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &from);

    if (recvResult) {
        auto& bytes = recvResult.value();
        // 处理响应数据
        // 注意：from 包含实际发送方地址
    }

    co_await socket.close();
}
```

**UDP 广播示例：**

```cpp
Coroutine udpBroadcast(IOScheduler* scheduler) {
    UdpSocket socket(scheduler);

    socket.create(IPType::IPV4);
    socket.option().handleNonBlock();

    // 启用广播
    int broadcast = 1;
    setsockopt(socket.handle().fd, SOL_SOCKET, SO_BROADCAST,
               &broadcast, sizeof(broadcast));

    // 广播地址
    Host broadcast_addr(IPType::IPV4, "255.255.255.255", 8080);

    const char* msg = "Broadcast message";
    co_await socket.sendto(msg, strlen(msg), broadcast_addr);

    co_await socket.close();
}
```

**UDP 多播示例：**

```cpp
Coroutine udpMulticast(IOScheduler* scheduler) {
    UdpSocket socket(scheduler);

    socket.create(IPType::IPV4);
    socket.option().handleReuseAddr();
    socket.option().handleNonBlock();

    // 绑定到多播端口
    socket.bind(Host(IPType::IPV4, "0.0.0.0", 8080));

    // 加入多播组
    struct ip_mreq mreq;
    inet_pton(AF_INET, "239.255.0.1", &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(socket.handle().fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
               &mreq, sizeof(mreq));

    char buffer[1024];
    while (true) {
        Host from;
        auto result = co_await socket.recvfrom(buffer, sizeof(buffer), &from);

        if (result) {
            // 处理多播消息
        }
    }

    co_await socket.close();
}
```

**TCP vs UDP 对比：**

| 特性 | TcpSocket | UdpSocket |
|------|-----------|-----------|
| 连接 | 面向连接 | 无连接 |
| 可靠性 | 可靠传输 | 不可靠 |
| 顺序 | 保证顺序 | 不保证顺序 |
| 流控 | 有流控 | 无流控 |
| 操作 | accept/connect/recv/send | recvfrom/sendto |
| 数据边界 | 字节流 | 数据报 |
| 开销 | 较大 | 较小 |
| 适用场景 | HTTP、文件传输 | DNS、视频流、游戏 |

---

## 文件 IO

### AsyncFile

异步文件操作封装。

```cpp
namespace galay::async {

enum class FileOpenMode : int {
    Read      = O_RDONLY,
    Write     = O_WRONLY | O_CREAT,
    ReadWrite = O_RDWR | O_CREAT,
    Append    = O_WRONLY | O_CREAT | O_APPEND,
    Truncate  = O_WRONLY | O_CREAT | O_TRUNC,
};

class AsyncFile {
public:
    // 构造
    AsyncFile(IOScheduler* scheduler);

    // 禁止拷贝，允许移动
    AsyncFile(const AsyncFile&) = delete;
    AsyncFile(AsyncFile&& other) noexcept;

    // 属性
    GHandle handle() const;
    bool isValid() const;

    // 同步操作
    std::expected<void, IOError> open(
        const std::string& path,
        FileOpenMode mode,
        int permissions = 0644
    );
    std::expected<size_t, IOError> size() const;
    std::expected<void, IOError> sync();

    // 异步操作
    FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);
    FileWriteAwaitable write(const char* buffer, size_t length, off_t offset = 0);
    CloseAwaitable close();
};

}
```

**文件读取示例：**

```cpp
Coroutine readFile(IOScheduler* scheduler, const std::string& path) {
    AsyncFile file(scheduler);

    auto openResult = file.open(path, FileOpenMode::Read);
    if (!openResult) {
        // 打开失败
        co_return;
    }

    auto sizeResult = file.size();
    if (!sizeResult) {
        co_return;
    }

    size_t fileSize = sizeResult.value();
    std::vector<char> buffer(fileSize);

    auto readResult = co_await file.read(buffer.data(), fileSize, 0);
    if (readResult) {
        auto& bytes = readResult.value();
        // 处理文件内容
    }

    co_await file.close();
}
```

**文件写入示例：**

```cpp
Coroutine writeFile(IOScheduler* scheduler, const std::string& path, const std::string& content) {
    AsyncFile file(scheduler);

    auto openResult = file.open(path, FileOpenMode::Write);
    if (!openResult) {
        co_return;
    }

    auto writeResult = co_await file.write(content.data(), content.size(), 0);
    if (writeResult) {
        size_t written = writeResult.value();
        // 写入成功
    }

    file.sync();  // 同步到磁盘
    co_await file.close();
}
```

**平台差异：**

| 平台 | 实现方式 | 注意事项 |
|-----|---------|---------|
| macOS (kqueue) | pread/pwrite | 同步调用，通过 kqueue 模拟异步 |
| Linux (epoll) | libaio + eventfd | 需要 O_DIRECT 标志 |
| Linux (io_uring) | io_uring 原生 | 真正的异步 IO |

---

## 错误处理

### IOError

IO 错误封装类。

```cpp
namespace galay::kernel {

enum IOErrorCode : uint32_t {
    kDisconnectError = 0,        // 连接断开
    kNotifyButSourceNotReady = 1, // 通知但源未就绪
    kRecvFailed = 2,             // 接收失败
    kSendFailed = 3,             // 发送失败
    kAcceptFailed = 4,           // 接受连接失败
    kConnectFailed = 5,          // 连接失败
    kBindFailed = 6,             // 绑定失败
    kListenFailed = 7,           // 监听失败
    kOpenFailed = 8,             // 打开文件失败
    kReadFailed = 9,             // 读取失败
    kWriteFailed = 10,           // 写入失败
    kStatFailed = 11,            // 获取文件状态失败
    kSyncFailed = 12,            // 同步失败
    kSeekFailed = 13,            // 定位失败
};

class IOError {
public:
    IOError(IOErrorCode io_error_code, uint32_t system_code);

    uint64_t code() const;        // 获取组合错误码
    std::string message() const;  // 获取错误消息

    static bool contains(uint64_t error, IOErrorCode code);
};

}
```

**错误处理示例：**

```cpp
auto result = co_await socket.recv(buffer, sizeof(buffer));

if (!result) {
    IOError& error = result.error();
    std::cerr << "Error: " << error.message() << std::endl;

    if (IOError::contains(error.code(), kDisconnectError)) {
        // 处理断开连接
    }
}
```

---

## 工具类

### Bytes

高效字节容器，支持零拷贝。

```cpp
namespace galay::kernel {

class Bytes {
public:
    // 构造
    Bytes();
    Bytes(size_t capacity);

    // 静态工厂
    static Bytes fromCString(const char* data, size_t size, size_t capacity);

    // 属性
    size_t size() const;
    size_t capacity() const;
    bool empty() const;

    // 数据访问
    const char* c_str() const;
    char* data();

    // 操作
    void append(const char* data, size_t size);
    void clear();
    void resize(size_t size);
};

}
```

### GHandle

通用句柄封装。

```cpp
namespace galay::kernel {

struct GHandle {
    int fd = -1;

    static GHandle invalid() { return GHandle{-1}; }
    bool isValid() const { return fd >= 0; }
};

}
```

---

## 编译选项

### 宏定义

| 宏 | 默认值 | 说明 |
|---|-------|------|
| `USE_KQUEUE` | macOS 自动定义 | 使用 kqueue 调度器 |
| `USE_EPOLL` | Linux 自动定义 | 使用 epoll 调度器 |
| `USE_IOURING` | 手动定义 | 使用 io_uring 调度器 |
| `GALAY_SCHEDULER_MAX_EVENTS` | 1024 | 单次处理最大事件数 |
| `GALAY_SCHEDULER_BATCH_SIZE` | 256 | 协程批处理大小 |
| `GALAY_SCHEDULER_CHECK_INTERVAL_MS` | 1 | 事件检查间隔 (毫秒) |
| `GALAY_SCHEDULER_QUEUE_DEPTH` | 4096 | io_uring 队列深度 |

### CMake 选项

```cmake
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(ENABLE_LOG "Enable spdlog logging" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)
```

---

## 最佳实践

### 1. 始终设置非阻塞模式

```cpp
socket.option().handleNonBlock();
```

### 2. 服务端设置地址重用

```cpp
listener.option().handleReuseAddr();
```

### 3. 正确处理部分发送

```cpp
size_t totalSent = 0;
while (totalSent < dataSize) {
    auto result = co_await socket.send(data + totalSent, dataSize - totalSent);
    if (!result) break;
    totalSent += result.value();
}
```

### 4. 检查接收结果

```cpp
auto result = co_await socket.recv(buffer, sizeof(buffer));
if (!result) {
    // 错误
} else if (result.value().size() == 0) {
    // 对端关闭
} else {
    // 正常数据
}
```

### 5. 资源清理

```cpp
// 始终在协程结束前关闭 socket
co_await socket.close();
```
