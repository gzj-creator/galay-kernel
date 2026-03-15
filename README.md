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
- 运行时编排：`Runtime`、`RuntimeBuilder`、`RuntimeHandle`
- 协程与任务：`Coroutine`、`Task<T>`、`JoinHandle<T>::join()/wait()`、`blockOn()`、`spawn()`、`spawnBlocking()`、`sleep(...)`
- 网络 IO：`galay::async::TcpSocket`、`galay::async::UdpSocket`
- 文件 IO：`galay::async::AsyncFile`；Linux epoll 下额外提供 `galay::async::AioFile`
- 低层组合扩展：`SequenceAwaitable<ResultT, InlineN>`、`AwaitableBuilder<ResultT, InlineN, FlowT>`、`ParseStatus`、`ByteQueueView`
- 并发原语：`AsyncMutex`、`MpscChannel<T>`、`UnsafeChannel<T>`、`AsyncWaiter<T>`
- 定时器：`TimerScheduler` + 线程安全分层时间轮
- 文件监控：`galay::async::FileWatcher`
- 向量 IO / 零拷贝：`readv` / `writev` / `sendfile`

## v3.1.0 非兼容升级

- 旧 `CustomAwaitable` / `CustomSequenceAwaitable` / `addCustom(...)` 扩展模型已移除，不再保留兼容层。
- 自定义组合 IO 统一改为 `SequenceAwaitable + SequenceStep + AwaitableBuilder`。
- 协议解析推荐使用 `AwaitableBuilder::parse(...) + ParseStatus + ByteQueueView`：
  - `ParseStatus::kNeedMore` 会自动重挂最近一个读步骤，不提前唤醒协程
  - `ParseStatus::kContinue` 会继续本地 parse loop，适合单次 `recv` 吃完粘包 backlog
  - `ByteQueueView` 用于累积半包、读取协议头和消费已解析字节
- 真实回归入口：
  - 线性组合：`test/T63-custom_sequence_awaitable.cc`
  - 半包不提前唤醒：`test/T76-sequence_parser_need_more.cc`
  - 粘包本地连读：`test/T77-sequence_parser_coalesced_frames.cc`

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

安装消费、`find_package` 与 CI 验证统一看 `docs/00-快速开始.md`、`docs/02-API参考.md`、`docs/07-常见问题.md`。

## 示例 / 测试 / benchmark 生成规则

- 示例：
  - `E1-SendfileExample` ~ `E5-UdpEcho` 始终由 `examples/include/*.cc` 生成
  - `E1-SendfileExampleImport` ~ `E9-TimerSleepImport` 仅在模块 target 生效时生成
- 测试：`test/T*.cc` 会按文件名直接生成同名 target，例如 `test/T13-async_mutex.cc` -> `T13-async_mutex`
- benchmark：`benchmark/CMakeLists.txt` 明确定义 `B1-ComputeScheduler` 到 `B14-SchedulerInjectedWakeup`

## 性能验证口径（2026-03-15）

- 当前本地 fresh 验证对应 `v3.1.0` worktree；历史 triplet 对比仍以 `baseline=cde3da1`、`refactored=v3.0.1(59bc155)` 为准
- 单 benchmark 的标准入口是 `scripts/run_single_benchmark_triplet.sh`
- 后端顺序固定为 `kqueue -> epoll -> io_uring`
- `scripts/run_benchmark_triplet.sh` 是单 backend 低层 orchestrator，`scripts/parse_benchmark_triplet.py` 只输出 `baseline | refactored`
- `B5-UdpClient` 只做 smoke/stability 检查，最终 UDP 性能结论以 `B6-Udp` 为准

## 当前已验证的命令（2026-03-15）

验证环境：macOS、AppleClang 17、CMake 默认 Release、后端为 kqueue。

```bash
python3 -m unittest \
  scripts.tests.test_run_test_matrix \
  scripts.tests.test_run_benchmark_matrix \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet

cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build --parallel

bash scripts/run_test_matrix.sh "$PWD/build" "$PWD/build/test_matrix_logs_2026_03_15_v310_final_rerun"

for name in E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho; do
  ./build/bin/$name
done

bash scripts/run_benchmark_matrix.sh \
  --build-dir "$PWD/build" \
  --log-root "$PWD/build/benchmark_matrix_logs_2026_03_15_v310_final"
```

实际结果：

- 脚本单测：`24/24` 通过
- 测试：全量 `test matrix` fresh 跑完，`74` 个日志全部生成，未出现新的 `FAILED` / `Segmentation fault` / `terminate called`
- 示例：
  - `E1-SendfileExample`：PASS
  - `E2-TcpEchoServer`：PASS
  - `E3-TcpClient`：PASS
  - `E4-CoroutineBasic`：PASS
  - `E5-UdpEcho`：PASS
- benchmark：
  - `B1` ~ `B14` fresh 跑完并生成当前机器日志
  - `B4/B5-Udp` 已恢复有效收发；`B5` 仍只作为 smoke / stability 检查
  - `B5-UdpClient` 本地 kqueue fresh 结果为 `100000 sent / 99507 received`、loss `0.493%`
  - `B6-Udp` 本地 kqueue fresh 结果为 `200000/200000`、loss `0.00%`、recv throughput `8.86656 MB/s`
- 模块 import 示例：当前环境 `ENABLE_CPP23_MODULES=OFF`，未生成 import target；最近一次专门的模块构建结论仍是 `Unix Makefiles + AppleClang` 下不会生成模块 target
- 安装与消费：最近一次专门 smoke 验证仍是 `2026-03-10`，细节见 `docs/00-快速开始.md`

## 当前限制

- 仓库当前没有 `ENABLE_LOG` 选项，也没有 `spdlog` 依赖链路
- `MpscChannel<T>` 没有 `close()`；发送端是同步 `bool send(...)`
- `HandleOption` 当前只公开 `handleBlock()`、`handleNonBlock()`、`handleReuseAddr()`、`handleReusePort()`
- benchmark 数字是当前机器单次运行结果，不是跨平台性能承诺

## 文档导航

- 主干层：优先使用 `docs/00-快速开始.md` 到 `docs/07-常见问题.md`
- 补充层：`docs/08-计算调度器.md` 到 `docs/20-异步同步原语.md` 现在只保留专题摘要、关键词、源码锚点与验证入口，不再承担完整主体叙述
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
