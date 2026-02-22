# Galay-Kernel

高性能 **C++23** 协程异步内核库，支持 `kqueue` / `epoll` / `io_uring`。

## 特性

- 协程友好：统一 `co_await` 异步 API
- 多后端 IO：macOS `kqueue`，Linux `epoll` / `io_uring`
- 文件 IO：`AsyncFile`（kqueue/io_uring）与 `AioFile`（epoll+libaio）
- 并发原语：`MpscChannel`、`UnsafeChannel`、`AsyncMutex`、`AsyncWaiter`
- 运行时管理：`Runtime` 管理多 IO/Compute 调度器
- 全局定时器：`TimerScheduler` + `sleep()`

## 文档导航

建议从 `docs/01-API文档.md` 开始：

1. [文档导航](docs/01-API文档.md)
2. [性能测试汇总](docs/02-性能测试.md)
3. [计算调度器](docs/03-计算调度器.md)
4. [UDP 性能测试](docs/04-UDP性能测试.md)
5. [调度器 API](docs/05-调度器.md)
6. [协程与超时](docs/06-协程.md)
7. [网络 IO](docs/07-网络IO.md)
8. [文件 IO](docs/08-文件IO.md)
9. [并发与通道](docs/09-并发.md)
10. [定时器调度器](docs/10-定时器调度器.md)
11. [RingBuffer](docs/11-环形缓冲区.md)
12. [零拷贝 sendfile](docs/12-零拷贝发送文件.md)
13. [Runtime](docs/13-运行时Runtime.md)
14. [文件监控](docs/14-文件监控.md)
15. [异步同步原语](docs/15-异步同步原语.md)

## 构建要求

- CMake 3.16+
- C++23 编译器（GCC 11+ / Clang 14+）
- `spdlog`（默认 `ENABLE_LOG=ON` 时需要）
- Linux:
  - `libaio`（epoll 文件 IO）
  - `liburing`（可选，启用 io_uring 时）

## 依赖安装（macOS / Homebrew）

```bash
brew install cmake spdlog
```

## 依赖安装（Ubuntu / Debian）

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ libspdlog-dev libaio-dev liburing-dev
```

## 拉取源码

```bash
git clone https://github.com/gzj-creator/galay-kernel.git
cd galay-kernel
```

## 构建

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

可执行文件默认输出到 `build/bin/`。

## 常用 CMake 选项

```cmake
option(BUILD_TESTS "Build test executables" ON)
option(BUILD_BENCHMARKS "Build benchmark executables" ON)
option(BUILD_EXAMPLES "Build example executables" ON)
option(ENABLE_LOG "Enable logging with spdlog" ON)
option(DISABLE_IOURING "Disable io_uring and use epoll on Linux" ON)
option(ENABLE_CPP23_MODULES "Enable experimental C++23 named modules support" OFF)
```

Linux 下默认 `DISABLE_IOURING=ON`，即优先走 `epoll`。如需 io_uring：

```bash
cmake .. -DDISABLE_IOURING=OFF
```

## C++23 模块（实验性）

项目新增模块门面 `galay.kernel`，支持 `import`/`export` 语法（实验性）。

启用方式：

```bash
cmake .. -DENABLE_CPP23_MODULES=ON
cmake --build . --parallel
```

模块能力生效规则（参考 `galay-rpc`）：

- CMake 版本需 `>= 3.28`
- Generator 需支持模块依赖扫描（推荐 `Ninja` / `Visual Studio`）
- 当前项目不支持 `AppleClang` 的模块构建路径
- 仅当以上条件满足时，`ENABLE_CPP23_MODULES=ON` 才会实际生效（`GALAY_KERNEL_CPP23_MODULES_EFFECTIVE=ON`）

示例：

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
cmake --build . --target E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho --parallel

# import 示例（需先 cmake .. -DENABLE_CPP23_MODULES=ON）
cmake --build . --target E1-SendfileExampleImport E2-TcpEchoServerImport E3-TcpClientImport E4-CoroutineBasicImport E5-UdpEchoImport E6-MpscChannelImport E7-UnsafeChannelImport E8-AsyncSyncImport E9-TimerSleepImport --parallel
```

限制：

- 需要 CMake 3.28+（`FILE_SET CXX_MODULES`）
- 推荐使用 Ninja/Visual Studio 生成器构建模块
- 当前项目暂不支持 AppleClang 的模块构建路径

### 模块支持更新（2026-02）

本次模块接口已统一为：

- `module;`
- `#include "galay-kernel/module/ModulePrelude.hpp"`
- `export module galay.kernel;`
- `export { #include ... }`

对应文件：

- `galay-kernel/module/galay.kernel.cppm`
- `galay-kernel/module/ModulePrelude.hpp`

推荐构建（Clang 20 + Ninja）：

```bash
cmake -S . -B build-mod -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang++ \
  -DENABLE_CPP23_MODULES=ON
cmake --build build-mod --target galay-kernel-modules --parallel
```

## 快速示例

```cpp
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/async/TcpSocket.h"

using namespace galay::kernel;
using namespace galay::async;

Coroutine echoSession(GHandle h) {
    TcpSocket client(h);
    client.option().handleNonBlock();

    char buf[4096];
    while (true) {
        auto r = co_await client.recv(buf, sizeof(buf));
        if (!r || r.value().size() == 0) {
            break;
        }
        auto& bytes = r.value();
        co_await client.send(bytes.c_str(), bytes.size());
    }

    co_await client.close();
}

Coroutine echoServer(IOScheduler* io) {
    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    if (!listener.bind(Host(IPType::IPV4, "0.0.0.0", 8080))) co_return;
    if (!listener.listen(1024)) co_return;

    while (true) {
        Host peer;
        auto a = co_await listener.accept(&peer);
        if (a) {
            io->spawn(echoSession(a.value()));
        }
    }
}

int main() {
    Runtime runtime;
    runtime.start();

    auto* io = runtime.getNextIOScheduler();
    io->spawn(echoServer(io));

    std::this_thread::sleep_for(std::chrono::hours(24));
    runtime.stop();
    return 0;
}
```

## 运行测试与基准

```bash
# 示例：运行若干目标
./build/bin/T21-Runtime
./build/bin/T13-AsyncMutex
./build/bin/B2-TcpServer 8080
./build/bin/B3-TcpClient -h 127.0.0.1 -p 8080 -c 100 -s 256 -d 10
```
