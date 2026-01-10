# 网络 IO API

## TcpSocket

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

### 服务端示例

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

### 客户端示例

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

## Host

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

## HandleOption

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

## UdpSocket

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

### 服务端示例

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

### 客户端示例

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

### UDP 广播示例

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

### UDP 多播示例

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

## TCP vs UDP 对比

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

---

## 性能压测数据

### TCP Socket 性能

| 平台 | IO 模型 | 100 并发 QPS | 平均吞吐量 | 峰值吞吐量 | 稳定性 |
|------|---------|-------------|-----------|-----------|--------|
| macOS | **kqueue** | **313,841** | **153.24 MB/s** | **155.93 MB/s** | ✅ 0% 错误 |
| Linux | io_uring | 302,893 | 147.90 MB/s | 150.78 MB/s | ✅ 0% 错误 |
| Linux | epoll | 177,102 | 86.48 MB/s | 88.30 MB/s | ✅ 0% 错误 |

**测试配置：**
- 消息大小: 256 bytes
- 测试时长: 10 seconds
- 测试模式: Echo (客户端发送 -> 服务器回显 -> 客户端接收)
- macOS 测试机器: Apple M4, 10 核心, 24 GB 内存

**性能排名：**
1. 🥇 **kqueue (macOS)**: 313,841 QPS (Apple M4)
2. 🥈 **io_uring (Linux)**: 302,893 QPS
3. 🥉 **epoll (Linux)**: 177,102 QPS

### UDP Socket 性能

| 平台 | IO 模型 | 并发客户端 | QPS | 吞吐量 | 丢包率 |
|------|---------|-----------|-----|--------|--------|
| macOS | kqueue | 100 | 35,008 | 8.55 MB/s | 0.00% |
| Linux | epoll | 100 | 35,082 | 8.56 MB/s | 0.00% |
| Linux | io_uring | 100 | 35,082 | 8.56 MB/s | 0.00% |

**测试配置：**
- 消息大小: 256 bytes
- 服务器工作协程: 4
- 每客户端消息数: 1000

**TCP vs UDP 性能对比：**

| 协议 | QPS | 吞吐量 | 差距倍数 |
|------|-----|--------|---------|
| TCP (kqueue/macOS) | 313,841 | 153.24 MB/s | 基准 |
| UDP (优化后) | 35,008 | 8.55 MB/s | 9倍差距 |

差距原因：TCP 连接复用 vs UDP 每次解析地址，TCP 有连接状态内核优化更好
