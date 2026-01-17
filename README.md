# Galay-Kernel

高性能 C++20 协程网络库，基于 kqueue/epoll/io_uring 实现异步 IO。

## 特性

- **高性能**: 单线程 26-28万 QPS，130+ MB/s 吞吐量
- **C++20 协程**: 基于标准协程实现，代码简洁直观
- **跨平台**: 支持 macOS (kqueue)、Linux (epoll/io_uring)
- **异步文件 IO**: 支持异步文件读写操作
- **零拷贝**: 高效的 `Bytes` 数据容器
- **可配置日志**: 基于 spdlog，支持编译期开关

## 平台支持

| 平台 | 网络 IO | 文件 IO |
|-----|--------|--------|
| macOS | kqueue | kqueue + pread/pwrite |
| Linux | epoll | libaio + eventfd |
| Linux (5.1+) | io_uring | io_uring 原生支持 |

## 性能基准

### 网络 IO

| 并发连接数 | QPS | 吞吐量 | 错误率 |
|-----------|-----|-------|-------|
| 100 | 279,569 | 136.5 MB/s | 0% |
| 500 | 275,722 | 134.6 MB/s | 0% |
| 1000 | 263,878 | 128.8 MB/s | 0% |

### MpscChannel (多生产者单消费者通道)

| 测试场景 | 吞吐量 | 备注 |
|---------|-------|------|
| 单生产者 | 1100 万 msg/s | 线程发送，协程接收 |
| 多生产者 (4线程) | 1400 万 msg/s | 4 线程并发发送 |
| 跨调度器 | 1600 万 msg/s | 生产者/消费者在不同调度器 |
| 持续压力 | 1300-1500 万 msg/s | 5 秒持续负载 |
| 平均延迟 | ~3 ms | 消息端到端延迟 |

> 详细报告请查看 [doc/benchmark_report.md](doc/benchmark_report.md)

## 快速开始

### 构建要求

- CMake 3.16+
- C++20 兼容编译器 (GCC 11+, Clang 14+, MSVC 2022+)
- Linux: liburing (可选，用于 io_uring 支持)
- Linux: libaio (用于 epoll 文件 IO)

### 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### 构建选项

| 选项 | 默认值 | 说明 |
|-----|-------|------|
| `BUILD_TESTS` | ON | 构建测试程序 |
| `BUILD_BENCHMARKS` | ON | 构建压测程序 |
| `ENABLE_LOG` | ON | 启用 spdlog 日志 |
| `BUILD_SHARED_LIBS` | OFF | 构建动态库 |

```bash
# 例如：禁用日志，只构建库
cmake .. -DENABLE_LOG=OFF -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF
```

## 使用示例

### Runtime 多调度器管理

Runtime 提供统一的调度器管理，支持多个 IO 调度器和计算调度器，并提供负载均衡策略。

```cpp
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/KqueueScheduler.h"
#include "galay-kernel/kernel/ComputeScheduler.h"

using namespace galay::kernel;

int main() {
    // 创建 Runtime，使用轮询负载均衡策略
    Runtime runtime(LoadBalanceStrategy::ROUND_ROBIN);

    // 添加 2 个 IO 调度器
    for (int i = 0; i < 2; ++i) {
        auto io_scheduler = std::make_unique<KqueueScheduler>();
        runtime.addIOScheduler(std::move(io_scheduler));
    }

    // 添加 4 个计算调度器
    for (int i = 0; i < 4; ++i) {
        auto compute_scheduler = std::make_unique<ComputeScheduler>();
        runtime.addComputeScheduler(std::move(compute_scheduler));
    }

    // 启动所有调度器
    runtime.start();

    // 使用负载均衡获取调度器
    auto* io_scheduler = runtime.getNextIOScheduler();
    auto* compute_scheduler = runtime.getNextComputeScheduler();

    // 提交任务
    io_scheduler->spawn(serverTask(&runtime));

    // ... wait for signal

    // 停止所有调度器
    runtime.stop();
}
```

### Echo 服务器

```cpp
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/KqueueScheduler.h"

using namespace galay::async;
using namespace galay::kernel;

Coroutine handleClient(IOScheduler* scheduler, GHandle handle) {
    TcpSocket client(scheduler, handle);
    client.option().handleNonBlock();

    char buffer[1024];
    while (true) {
        auto result = co_await client.recv(buffer, sizeof(buffer));
        if (!result || result.value().size() == 0) break;

        co_await client.send(result.value().c_str(), result.value().size());
    }
    co_await client.close();
}

Coroutine server(IOScheduler* scheduler) {
    TcpSocket listener(scheduler);
    listener.create(IPType::IPV4);
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();
    listener.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
    listener.listen(1024);

    while (true) {
        Host clientHost;
        auto result = co_await listener.accept(&clientHost);
        if (result) {
            scheduler->spawn(handleClient(scheduler, result.value()));
        }
    }
}

int main() {
    KqueueScheduler scheduler;
    scheduler.start();
    scheduler.spawn(server(&scheduler));
    // ... wait for signal
    scheduler.stop();
}
```

### Echo 客户端

```cpp
Coroutine client(IOScheduler* scheduler) {
    TcpSocket socket(scheduler);
    socket.create(IPType::IPV4);
    socket.option().handleNonBlock();

    co_await socket.connect(Host(IPType::IPV4, "127.0.0.1", 8080));

    co_await socket.send("Hello", 5);

    char buffer[1024];
    auto result = co_await socket.recv(buffer, sizeof(buffer));

    co_await socket.close();
}
```

## 项目结构

```
galay-kernel/
├── galay-kernel/           # 核心库源码
│   ├── kernel/             # 协程调度器
│   │   ├── Coroutine.h/cc  # 协程实现
│   │   ├── Scheduler.h/cc  # 调度器基类
│   │   ├── Runtime.h/cc    # 多调度器管理器
│   │   ├── ComputeScheduler.h/cc # 计算调度器
│   │   ├── Awaitable.h/cc  # 可等待对象
│   │   ├── Waker.h/cc      # 协程唤醒器
│   │   └── KqueueScheduler.h/cc  # kqueue 调度器
│   ├── async/              # 异步 IO 封装
│   │   └── TcpSocket.h/cc  # TCP Socket
│   └── common/             # 公共组件
│       ├── Defn.hpp        # 平台定义
│       ├── Buffer.h/cc     # 缓冲区
│       ├── Bytes.h/cc      # 字节容器
│       ├── Strategy.hpp/inl # 负载均衡策略
│       ├── Error.h/cc      # 错误处理
│       ├── Host.hpp        # 地址封装
│       ├── HandleOption.h/cc # 句柄选项
│       └── Log.h           # 日志封装
├── test/                   # 测试代码
│   └── test_runtime.cc     # Runtime 测试
├── benchmark/              # 压测工具
│   ├── bench_server.cc     # 压测服务器
│   └── bench_client.cc     # 压测客户端
├── doc/                    # 文档
│   └── benchmark_report.md # 压测报告
└── CMakeLists.txt          # 构建配置
```

## 运行压测

```bash
# 终端 1: 启动服务器
./bin/bench_server 8080

# 终端 2: 运行压测
./bin/bench_client -c 1000 -s 256 -d 10

# 压测参数:
#   -h <host>        服务器地址 (默认: 127.0.0.1)
#   -p <port>        服务器端口 (默认: 8080)
#   -c <connections> 并发连接数 (默认: 100)
#   -s <size>        消息大小 (默认: 256)
#   -d <duration>    测试时长秒 (默认: 10)
```

## API 参考

### Runtime

```cpp
class Runtime {
    // 构造
    explicit Runtime(LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);

    // 添加调度器 (必须在 start() 之前)
    bool addIOScheduler(std::unique_ptr<IOScheduler> scheduler);
    bool addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler);

    // 生命周期管理
    void start();  // 启动所有调度器
    void stop();   // 停止所有调度器
    bool isRunning() const;

    // 获取调度器
    IOScheduler* getIOScheduler(size_t index);           // 按索引获取
    ComputeScheduler* getComputeScheduler(size_t index); // 按索引获取
    IOScheduler* getNextIOScheduler();                   // 负载均衡获取
    ComputeScheduler* getNextComputeScheduler();         // 负载均衡获取

    // 查询
    size_t getIOSchedulerCount() const;
    size_t getComputeSchedulerCount() const;
    LoadBalanceStrategy getLoadBalanceStrategy() const;
};

// 负载均衡策略
enum class LoadBalanceStrategy {
    ROUND_ROBIN,  // 轮询
    RANDOM        // 随机
};
```

### TcpSocket

```cpp
class TcpSocket {
    // 构造
    TcpSocket(IOScheduler* scheduler);
    TcpSocket(IOScheduler* scheduler, GHandle handle);

    // 同步操作
    std::expected<void, IOError> create(IPType type = IPType::IPV4);
    std::expected<void, IOError> bind(const Host& host);
    std::expected<void, IOError> listen(int backlog = 128);
    HandleOption option();  // 获取选项配置器

    // 异步操作 (co_await)
    AcceptAwaitable accept(Host* clientHost);
    ConnectAwaitable connect(const Host& host);
    RecvAwaitable recv(char* buffer, size_t length);
    SendAwaitable send(const char* buffer, size_t length);
    CloseAwaitable close();
};
```

### AsyncFile

```cpp
class AsyncFile {
    // 构造
    AsyncFile(IOScheduler* scheduler);

    // 同步操作
    std::expected<void, IOError> open(const std::string& path, FileOpenMode mode, int permissions = 0644);
    std::expected<size_t, IOError> size() const;
    std::expected<void, IOError> sync();

    // 异步操作 (co_await)
    FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);
    FileWriteAwaitable write(const char* buffer, size_t length, off_t offset = 0);
    CloseAwaitable close();
};

// 文件打开模式
enum class FileOpenMode {
    Read,       // 只读
    Write,      // 只写 (创建)
    ReadWrite,  // 读写 (创建)
    Append,     // 追加
    Truncate,   // 截断
};
```

### HandleOption

```cpp
class HandleOption {
    std::expected<void, IOError> handleBlock();      // 阻塞模式
    std::expected<void, IOError> handleNonBlock();   // 非阻塞模式
    std::expected<void, IOError> handleReuseAddr();  // SO_REUSEADDR
    std::expected<void, IOError> handleReusePort();  // SO_REUSEPORT
};
```

### Logger

```cpp
// 日志宏 (需要 ENABLE_LOG=ON)
LogTrace("message {}", arg);
LogDebug("message {}", arg);
LogInfo("message {}", arg);
LogWarn("message {}", arg);
LogError("message {}", arg);
LogCritical("message {}", arg);

// 运行时控制
Logger::enable();   // 启用日志
Logger::disable();  // 禁用日志
```

## 许可证

MIT License
