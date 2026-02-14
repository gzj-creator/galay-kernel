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
- Linux:
  - `libaio`（epoll 文件 IO）
  - `liburing`（可选，启用 io_uring 时）

## 构建

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

可执行文件默认输出到 `build/bin/`。

## 常用 CMake 选项

```cmake
option(BUILD_TESTS "Build test executables" ON)
option(BUILD_BENCHMARKS "Build benchmark executables" ON)
option(BUILD_EXAMPLES "Build example executables" ON)
option(ENABLE_LOG "Enable logging with spdlog" ON)
option(DISABLE_IOURING "Disable io_uring and use epoll on Linux" ON)
```

Linux 下默认 `DISABLE_IOURING=ON`，即优先走 `epoll`。如需 io_uring：

```bash
cmake .. -DDISABLE_IOURING=OFF
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

