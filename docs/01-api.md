# Galay-Kernel API 文档

## 概述

Galay-Kernel 是一个高性能异步 IO 框架，采用事件驱动的协程模型。

## 架构

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

## 协程工作流程

```
1. 创建协程函数 (返回 Coroutine)
2. 调用 scheduler.spawn(coroutine) 提交到调度器
3. 协程执行到 co_await 时挂起
4. IO 事件完成后，调度器唤醒协程继续执行
5. 协程执行完毕或遇到 co_return 结束
```

## 文档目录

| 文档 | 说明 |
|------|------|
| [02-benchmark.md](02-benchmark.md) | MpscChannel 性能压测数据 |
| [03-compute-scheduler.md](03-compute-scheduler.md) | ComputeScheduler 计算调度器设计 |
| [04-udp-benchmark.md](04-udp-benchmark.md) | UDP 性能压测数据 |
| [05-scheduler.md](05-scheduler.md) | 调度器 API (IOScheduler, KqueueScheduler, EpollScheduler, IOUringScheduler) |
| [06-coroutine.md](06-coroutine.md) | 协程 API (Coroutine, Waker, 超时机制) |
| [07-network-io.md](07-network-io.md) | 网络 IO API (TcpSocket, UdpSocket, Host, HandleOption) |
| [08-file-io.md](08-file-io.md) | 文件 IO API (AsyncFile) |
| [09-concurrency.md](09-concurrency.md) | 并发工具 API (MpscChannel, Bytes, GHandle) |

## 快速开始

### TCP Echo 服务器

```cpp
#include "galay-kernel/kernel/KqueueScheduler.h"
#include "galay-kernel/async/TcpSocket.h"

using namespace galay::kernel;
using namespace galay::async;

Coroutine handleClient(IOScheduler* scheduler, GHandle handle) {
    TcpSocket client(scheduler, handle);
    client.option().handleNonBlock();

    char buffer[4096];
    while (true) {
        auto result = co_await client.recv(buffer, sizeof(buffer));
        if (!result || result.value().size() == 0) break;
        co_await client.send(result.value().c_str(), result.value().size());
    }
    co_await client.close();
}

Coroutine echoServer(IOScheduler* scheduler) {
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
    scheduler.spawn(echoServer(&scheduler));

    // 运行直到收到信号
    std::this_thread::sleep_for(std::chrono::hours(24));

    scheduler.stop();
    return 0;
}
```

### 使用 MpscChannel 进行协程通信

```cpp
#include "galay-kernel/concurrency/MpscChannel.h"

MpscChannel<int> channel;

// 生产者线程
void producer() {
    auto token = MpscChannel<int>::getToken();
    for (int i = 0; i < 1000; ++i) {
        channel.send(i, token);
    }
}

// 消费者协程
Coroutine consumer() {
    int count = 0;
    while (count < 1000) {
        auto value = co_await channel.recv();
        if (value) {
            ++count;
        }
    }
}
```

## 编译选项

### 宏定义

| 宏 | 默认值 | 说明 |
|---|-------|------|
| `USE_KQUEUE` | macOS 自动定义 | 使用 kqueue 调度器 |
| `USE_EPOLL` | Linux 自动定义 | 使用 epoll 调度器 |
| `USE_IOURING` | 手动定义 | 使用 io_uring 调度器 |

### CMake 选项

```cmake
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(ENABLE_LOG "Enable spdlog logging" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)
```
