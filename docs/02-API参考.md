# 01-API文档

`galay-kernel` 是一个基于 **C++23 协程** 的异步内核库，提供网络 IO、文件 IO、调度器、定时器和协程并发原语。

## 推荐阅读顺序

### 新手入门

1. [快速开始](00-快速开始.md) - 依赖安装、编译、运行第一个示例
2. [示例代码](17-示例代码.md) - 常见使用场景的完整示例
3. [网络 IO](07-网络IO.md) - TCP/UDP 网络编程
4. [协程与超时](06-协程.md) - 协程基础与超时机制

### 核心概念

5. [架构设计](16-架构设计.md) - 整体架构与设计原理
6. [Runtime](13-运行时Runtime.md) - 多调度器统一管理
7. [调度器](05-调度器.md) - IOScheduler 与 Scheduler 基类
8. [计算调度器](03-计算调度器.md) - CPU 任务调度

### 进阶使用

9. [文件 IO](08-文件IO.md) - 异步文件读写
10. [并发与通道](09-并发.md) - MpscChannel、UnsafeChannel
11. [异步同步原语](15-异步同步原语.md) - AsyncMutex、AsyncWaiter
12. [定时器调度器](10-定时器调度器.md) - 全局定时器与 sleep

### 性能优化

13. [高级主题](19-高级主题.md) - 性能优化、最佳实践、高级用法
14. [性能测试](02-性能测试.md) - TCP/UDP 性能测试结果
15. [零拷贝 sendfile](12-零拷贝发送文件.md) - sendfile 专项
16. [RingBuffer](11-环形缓冲区.md) - 环形缓冲区实现

### 故障排查

17. [常见问题](18-常见问题.md) - FAQ 与故障排查
18. [UDP 性能测试](04-UDP性能测试.md) - UDP 专项测试
19. [文件监控](14-文件监控.md) - FileWatcher (inotify/kqueue)

## 模块覆盖清单

| 模块 | 说明 | 文档 |
|---|---|---|
| Scheduler | 调度器抽象基类 | [05-调度器.md](05-调度器.md) |
| IOScheduler | 平台 IO 事件调度 | [05-调度器.md](05-调度器.md) |
| ComputeScheduler | CPU 任务调度 | [03-计算调度器.md](03-计算调度器.md) |
| Runtime | 多 IO/Compute 调度器管理 | [13-运行时Runtime.md](13-运行时Runtime.md) |
| Coroutine / Timeout / Sleep | 协程生命周期与超时 | [06-协程.md](06-协程.md) |
| TcpSocket / UdpSocket / Host | 网络编程 API | [07-网络IO.md](07-网络IO.md) |
| AsyncFile / AioFile | 异步文件 IO | [08-文件IO.md](08-文件IO.md) |
| FileWatcher | 文件监控（inotify / kqueue） | [14-文件监控.md](14-文件监控.md) |
| MpscChannel / UnsafeChannel / Bytes | 协程并发与数据容器 | [09-并发.md](09-并发.md) |
| AsyncMutex / AsyncWaiter | 异步同步原语 | [15-异步同步原语.md](15-异步同步原语.md) |
| TimerScheduler | 全局线程安全定时器 | [10-定时器调度器.md](10-定时器调度器.md) |
| RingBuffer | scatter-gather 缓冲区 | [11-环形缓冲区.md](11-环形缓冲区.md) |
| Sendfile | 零拷贝文件发送 | [12-零拷贝发送文件.md](12-零拷贝发送文件.md) |

## 快速开始

### 构建

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### 最小 Echo 服务（Runtime + TcpSocket）

```cpp
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/async/TcpSocket.h"

using namespace galay::kernel;
using namespace galay::async;

Coroutine handleClient(GHandle handle) {
    TcpSocket client(handle);
    client.option().handleNonBlock();

    char buffer[4096];
    while (true) {
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult || recvResult.value() == 0) {
            break;
        }
        if (auto __await_result = co_await client.send(buffer, recvResult.value()); !__await_result) {
            // 错误处理：记录日志、重试或提前返回
        }
    }

    co_await client.close();
}

Coroutine server(IOScheduler* scheduler) {
    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    auto bindResult = listener.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
    if (!bindResult) {
        co_return;
    }

    auto listenResult = listener.listen(1024);
    if (!listenResult) {
        co_return;
    }

    while (true) {
        Host peer;
        auto acceptResult = co_await listener.accept(&peer);
        if (!acceptResult) {
            continue;
        }
        scheduler->spawn(handleClient(acceptResult.value()));
    }
}

int main() {
    Runtime runtime = RuntimeBuilder().build(); // 零配置：自动创建 IO/Compute 调度器
    runtime.start();

    auto* io = runtime.getNextIOScheduler();
    io->spawn(server(io));

    std::this_thread::sleep_for(std::chrono::hours(24));
    runtime.stop();
    return 0;
}
```

## 构建选项

```cmake
option(BUILD_TESTS "Build test executables" ON)
option(BUILD_BENCHMARKS "Build benchmark executables" ON)
option(BUILD_EXAMPLES "Build example executables" ON)
option(ENABLE_LOG "Enable logging with spdlog" ON)
option(DISABLE_IOURING "Disable io_uring and use epoll on Linux" ON)
option(ENABLE_CPP23_MODULES "Enable experimental C++23 named modules support" OFF)
```

## 平台后端

- macOS: `USE_KQUEUE`
- Linux + `DISABLE_IOURING=ON`（默认）: `USE_EPOLL`
- Linux + `DISABLE_IOURING=OFF` 且系统有 `liburing`: `USE_IOURING`

## C++23 模块语法（实验性）

已提供模块门面 `galay.kernel`，支持 `import` / `export`。

```bash
cmake .. -DENABLE_CPP23_MODULES=ON
cmake --build . --parallel
```

模块能力生效规则（参考 `galay-rpc`）：

- CMake 版本需 `>= 3.28`
- Generator 需支持模块依赖扫描（推荐 `Ninja` / `Visual Studio`）
- 当前项目不支持 `AppleClang` 的模块构建路径
- 仅当以上条件满足时，`ENABLE_CPP23_MODULES=ON` 才会实际生效（`GALAY_KERNEL_CPP23_MODULES_EFFECTIVE=ON`）

```cpp
import galay.kernel;

int main() {
    galay::kernel::Runtime runtime = galay::kernel::RuntimeBuilder().build();
    return 0;
}
```

模块示例目标（启用 `ENABLE_CPP23_MODULES` 后可构建）：

- `E1-SendfileExampleImport`
- `E2-TcpEchoServerImport`
- `E3-TcpClientImport`
- `E4-CoroutineBasicImport`
- `E5-UdpEchoImport`
- `E6-MpscChannelImport`
- `E7-UnsafeChannelImport`
- `E8-AsyncSyncImport`
- `E9-TimerSleepImport`

示例目录结构（参考 `galay-mysql`）：

```text
examples/
├── common/   # 示例共享配置
├── include/  # 传统头文件版本
└── import/   # C++23 模块 import 版本
```

include 示例目标（默认可构建）：

- `E1-SendfileExample` -> `examples/include/E1-SendfileExample.cc`
- `E2-TcpEchoServer` -> `examples/include/E2-TcpEchoServer.cc`
- `E3-TcpClient` -> `examples/include/E3-TcpClient.cc`
- `E4-CoroutineBasic` -> `examples/include/E4-CoroutineBasic.cc`
- `E5-UdpEcho` -> `examples/include/E5-UdpEcho.cc`

示例构建命令：

```bash
# include 示例（默认）
cmake --build . --target E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho --parallel

# import 示例（需先 cmake .. -DENABLE_CPP23_MODULES=ON）
cmake --build . --target E1-SendfileExampleImport E2-TcpEchoServerImport E3-TcpClientImport E4-CoroutineBasicImport E5-UdpEchoImport E6-MpscChannelImport E7-UnsafeChannelImport E8-AsyncSyncImport E9-TimerSleepImport --parallel
```

说明：

- 需要 CMake 3.28+
- 推荐使用 Ninja/Visual Studio 生成器构建模块
- 当前不支持 AppleClang 模块构建路径
