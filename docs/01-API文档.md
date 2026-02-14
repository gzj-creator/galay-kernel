# 01. 文档导航

`galay-kernel` 是一个基于 **C++23 协程** 的异步内核库，提供网络 IO、文件 IO、调度器、定时器和协程并发原语。

## 推荐阅读顺序（参考 galay-redis）

1. [快速开始（本页）](01-API文档.md)
2. [使用示例：网络 IO](07-网络IO.md)、[文件 IO](08-文件IO.md)、[并发](09-并发.md)
3. [模块介绍：调度器](05-调度器.md)、[计算调度器](03-计算调度器.md)、[Runtime](13-运行时Runtime.md)
4. [运行原理：协程与超时](06-协程.md)、[全局定时器](10-定时器调度器.md)、[RingBuffer](11-环形缓冲区.md)
5. [性能与专项](02-性能测试.md)、[UDP压测](04-UDP性能测试.md)、[sendfile专项](12-零拷贝发送文件.md)

## 模块覆盖清单

| 模块 | 说明 | 文档 |
|---|---|---|
| Scheduler / IOScheduler | 平台 IO 事件调度 | [05-调度器.md](05-调度器.md) |
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
cmake --build . -j
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
        if (!recvResult || recvResult.value().size() == 0) {
            break;
        }
        auto& bytes = recvResult.value();
        co_await client.send(bytes.c_str(), bytes.size());
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
    Runtime runtime; // 零配置：自动创建 IO/Compute 调度器
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
cmake --build . -j
```

模块能力生效规则（参考 `galay-rpc`）：

- CMake 版本需 `>= 3.28`
- Generator 需支持模块依赖扫描（推荐 `Ninja` / `Visual Studio`）
- 当前项目不支持 `AppleClang` 的模块构建路径
- 仅当以上条件满足时，`ENABLE_CPP23_MODULES=ON` 才会实际生效（`GALAY_KERNEL_CPP23_MODULES_EFFECTIVE=ON`）

```cpp
import galay.kernel;

int main() {
    galay::kernel::Runtime runtime;
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
example/
├── common/   # 示例共享配置
├── include/  # 传统头文件版本
└── import/   # C++23 模块 import 版本
```

include 示例目标（默认可构建）：

- `E1-SendfileExample` -> `example/include/E1-SendfileExample.cc`
- `E2-TcpEchoServer` -> `example/include/E2-TcpEchoServer.cc`
- `E3-TcpClient` -> `example/include/E3-TcpClient.cc`
- `E4-CoroutineBasic` -> `example/include/E4-CoroutineBasic.cc`
- `E5-UdpEcho` -> `example/include/E5-UdpEcho.cc`

示例构建命令：

```bash
# include 示例（默认）
cmake --build . --target E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho -j

# import 示例（需先 cmake .. -DENABLE_CPP23_MODULES=ON）
cmake --build . --target E1-SendfileExampleImport E2-TcpEchoServerImport E3-TcpClientImport E4-CoroutineBasicImport E5-UdpEchoImport E6-MpscChannelImport E7-UnsafeChannelImport E8-AsyncSyncImport E9-TimerSleepImport -j
```

说明：

- 需要 CMake 3.28+
- 推荐使用 Ninja/Visual Studio 生成器构建模块
- 当前不支持 AppleClang 模块构建路径
