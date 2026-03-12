# Galay-Kernel

`galay-kernel` 是一个以 C++23 协程为核心的异步运行时与基础设施库。当前文档把 `galay-kernel/` 公开头文件、CMake 导出 target、`examples/`、`test/` 与 `benchmark/` 作为唯一真相来源，并把文档页降级为说明层。

## 当前真相源

当仓库内容冲突时，本仓库按以下顺序判断真相：

1. `galay-kernel/` 下的公开头文件与导出 target
2. `galay-kernel/` 下的实现
3. `examples/`
4. `test/`
5. `benchmark/`
6. `README.md` 与 `docs/*.md`

## 能力概览

- IO 调度：`EpollScheduler` / `IOUringScheduler` / `KqueueScheduler` / `IOCP`
- 计算调度：`ComputeScheduler`
- 运行时编排：`Runtime`、`RuntimeBuilder`
- 协程与等待：`Coroutine`、`spawn()`、`sleep(...)`
- 网络 IO：`galay::async::TcpSocket`、`galay::async::UdpSocket`
- 文件 IO：`galay::async::AsyncFile`；Linux epoll 下额外提供 `galay::async::AioFile`
- 并发原语：`AsyncMutex`、`MpscChannel<T>`、`UnsafeChannel<T>`、`AsyncWaiter<T>`
- 定时器：`TimerScheduler` + 线程安全分层时间轮
- 文件监控：`galay::async::FileWatcher`
- 向量 IO / 零拷贝：`readv` / `writev` / `sendfile`

## 构建前提

- 编译器：支持 C++23
- CMake：`>= 3.16`
- 命名模块：`ENABLE_CPP23_MODULES=ON` 只在 `CMake >= 3.28`、生成器支持模块、编译器不是 AppleClang 时才会真正生效
- 头文件依赖：编译器需要能找到 `<concurrentqueue/moodycamel/*.h>`；仓库当前不会自动下载该依赖
- Linux：
  - `DISABLE_IOURING=ON` 时使用 epoll；异步文件 IO 依赖 `libaio`
  - `DISABLE_IOURING=OFF` 且系统存在 `liburing` 时使用 io_uring
- macOS：当前自动选择 kqueue
- Windows：当前自动选择 IOCP

## 快速构建

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build --parallel
```

## 安装与消费

导出 target：

- `galay-kernel`
- `galay-kernel-modules`
  - 仅当 `ENABLE_CPP23_MODULES=ON`
  - 且 `GALAY_KERNEL_CPP23_MODULES_EFFECTIVE=TRUE`

安装命令：

```bash
cmake --install build --prefix /tmp/galay-kernel-install
```

安装后的包元数据会显式区分两类头文件：

- `GALAY_KERNEL_SUPPORTED_HEADERS`：受当前文档与兼容性承诺约束的 direct-include 入口头
- `GALAY_KERNEL_INTERNAL_HEADERS`：仅因内联 / 模板依赖而随包安装的内部头，避免直接 `#include`
- `GALAY_KERNEL_PACKAGE_CONSUMER_FIXTURE_DIR`：安装树中的最小 `find_package` consumer fixture

消费方式：

```cmake
find_package(galay-kernel CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE galay-kernel::galay-kernel)
```

仓库内的 source-controlled consumer fixture 位于 `test/package-consumer/`。

更完整的企业接入说明见 `docs/21-企业接入与验证.md`。

## 示例 / 测试 / benchmark 生成规则

- 示例：
  - `E1-SendfileExample` ~ `E5-UdpEcho` 始终由 `examples/include/*.cc` 生成
  - `E1-SendfileExampleImport` ~ `E9-TimerSleepImport` 仅在模块 target 生效时生成
- 测试：`test/T*.cc` 会按文件名直接生成同名 target，例如 `test/T13-async_mutex.cc` -> `T13-async_mutex`
- benchmark：`benchmark/CMakeLists.txt` 明确定义 `B1-ComputeScheduler` 到 `B13-Sendfile`

## 当前已验证的命令（2026-03-10）

验证环境：macOS、AppleClang 17、CMake 默认 Release、后端为 kqueue。

```bash
cmake -S . -B build-docverify -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build-docverify --target \
  T9-file_watcher T13-async_mutex T14-mpsc_channel T18-timer_scheduler \
  T19-readv_writev T23-sendfile_basic T27-runtime_stress T42-runtime_strict_scheduler_counts \
  E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho \
  B8-MpscChannel B10-Ringbuffer B13-Sendfile --parallel

python3 scripts/check-doc-links.py
cmake --install build-docverify --prefix "$PWD/build-docverify/install"
cmake -S test/package-consumer -B build-docverify/package-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-docverify/install"
cmake --build build-docverify/package-consumer --parallel
./build-docverify/package-consumer/galay_consumer
```

实际结果：

- 文档：`scripts/check-doc-links.py` 通过，输出 `doc check passed: 24 markdown files`
- 测试：
  - `T9-file_watcher`：`3/3` 通过
  - `T13-async_mutex`：`11/11` 通过
  - `T14-mpsc_channel`：`13/13` 通过
  - `T18-timer_scheduler`：`8/8` 通过
  - `T19-readv_writev`：PASS
  - `T23-sendfile_basic`：`4/4` 通过
  - `T27-runtime_stress`：`5/5` 通过
  - `T42-runtime_strict_scheduler_counts`：PASS
- 示例：
  - `E1-SendfileExample`：PASS
  - `E2-TcpEchoServer`：PASS
  - `E3-TcpClient`：PASS
  - `E4-CoroutineBasic`：PASS
  - `E5-UdpEcho`：PASS
- benchmark：
  - `B8-MpscChannel`：已运行并输出当前吞吐 / 延迟数据
  - `B10-Ringbuffer`：已运行并输出当前吞吐 / iovec 指标
  - `B13-Sendfile`：已运行并输出当前 sendfile / read+send 对比数据
- 安装与消费：
  - `cmake --install` 成功
  - `find_package(galay-kernel CONFIG REQUIRED)` 烟雾测试成功
  - `GALAY_KERNEL_SUPPORTED_HEADERS` / `GALAY_KERNEL_INTERNAL_HEADERS` 元数据存在
  - 运行安装后的最小 consumer：`consumer exit=0`

模块配置额外验证：

```bash
cmake -S . -B build-docverify-modules -DENABLE_CPP23_MODULES=ON
cmake --build build-docverify-modules --target E6-MpscChannelImport
```

当前真实结果：

- CMake 警告生成器 `Unix Makefiles` 不支持模块
- CMake 警告 `AppleClang` 不支持该项目的模块配置
- 因而 `E6-MpscChannelImport` 当前环境下不存在，`make` 报 `No rule to make target`

## 当前限制

- 仓库当前没有 `ENABLE_LOG` 选项，也没有 `spdlog` 依赖链路
- `MpscChannel<T>` 没有 `close()`；发送端是同步 `bool send(...)`
- `HandleOption` 当前只公开 `handleBlock()`、`handleNonBlock()`、`handleReuseAddr()`、`handleReusePort()`
- benchmark 数字是当前机器单次运行结果，不是跨平台性能承诺

## 文档导航

- 主干层：优先使用 `docs/00-快速开始.md` 到 `docs/07-常见问题.md`
- 补充层：`docs/08-计算调度器.md` 到 `docs/21-企业接入与验证.md` 现在只保留专题摘要、关键词、源码锚点与验证入口，不再承担完整主体叙述
- 总览：`docs/README.md`
- 快速开始：`docs/00-快速开始.md`
- 架构设计：`docs/01-架构设计.md`
- API 参考：`docs/02-API参考.md`
- 使用指南：`docs/03-使用指南.md`
- 示例代码：`docs/04-示例代码.md`
- 性能测试：`docs/05-性能测试.md`
- 高级主题：`docs/06-高级主题.md`
- 常见问题：`docs/07-常见问题.md`

优先落到主干页的查询类型：

- `galay.kernel` / Runtime / Scheduler / Coroutine / TimerScheduler：先看 `docs/01-架构设计.md`、`docs/02-API参考.md`、`docs/03-使用指南.md`
- `Bytes` / `StringMetaData` / `Buffer` / `RingBuffer` / `Host` / `IOError`：先看 `docs/02-API参考.md`、`docs/07-常见问题.md`
- TcpSocket / UdpSocket / AsyncFile / FileWatcher：先看 `docs/02-API参考.md`、`docs/03-使用指南.md`
- MpscChannel / UnsafeChannel / AsyncMutex / AsyncWaiter：先看 `docs/02-API参考.md`、`docs/03-使用指南.md`、`docs/07-常见问题.md`
- benchmark / 当前性能事实：先看 `docs/05-性能测试.md`
- `find_package` / 安装消费 / 企业接入：先看 `docs/00-快速开始.md`、`docs/02-API参考.md`、`docs/07-常见问题.md`

补充专题落地页：

- `docs/08-计算调度器.md`
- `docs/09-UDP性能测试.md`
- `docs/10-调度器.md`
- `docs/11-协程.md`
- `docs/12-网络IO.md`
- `docs/13-文件IO.md`
- `docs/14-并发.md`
- `docs/15-定时器调度器.md`
- `docs/16-环形缓冲区.md`
- `docs/17-零拷贝发送文件.md`
- `docs/18-运行时Runtime.md`
- `docs/19-文件监控.md`
- `docs/20-异步同步原语.md`
- `docs/21-企业接入与验证.md`
