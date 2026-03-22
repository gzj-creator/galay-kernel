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
- 任务模型：`Task<T>`、`Task<void>::then(...)`、`JoinHandle<T>::join()/wait()`、`blockOn()`、`spawn()`、`spawnBlocking()`、`sleep(...)`
- 网络 IO：`galay::async::TcpSocket`、`galay::async::UdpSocket`
- 文件 IO：`galay::async::AsyncFile`；Linux epoll 下额外提供 `galay::async::AioFile`
- 低层组合扩展：`SequenceAwaitable<ResultT, InlineN>`、`AwaitableBuilder<ResultT, InlineN, FlowT>`、`StateMachineAwaitable<MachineT>`、`MachineAction<ResultT>`、`ParseStatus`、`ByteQueueView`
- 并发原语：`AsyncMutex`、`MpscChannel<T>`、`UnsafeChannel<T>`、`AsyncWaiter<T>`
- 定时器：`TimerScheduler` + 线程安全分层时间轮
- 文件监控：`galay::async::FileWatcher`
- 向量 IO / 零拷贝：`readv` / `writev` / `sendfile`

## v3.4.1 更新

- `HandleOption` 新增 `handleTcpNoDelay()`，把 `TCP_NODELAY` 能力收敛到统一的 socket option 接口，便于上层 HTTP / WebSocket / HTTP/2 直接复用。
- 新增回归测试 `test/T104-handle_option_tcp_nodelay.cc`，验证 `TCP_NODELAY` 已被实际开启。

## v3.4.0 更新

- `Runtime::spawnBlocking(...)` 底层不再为每个阻塞任务单独 `detach` 一个线程，现已改为独立的 bounded elastic blocking executor：有空闲 worker 时复用，没有空闲 worker 时按需扩容，到上限后排队。
- 源码分发版本元数据已对齐到 `3.4.0`。

## v3.3.0 更新

- sequence owner 现在支持同一 `IOController` 上一个只读 sequence 和一个只写 sequence 并发共存；双向 `StateMachineAwaitable` 仍保持双向独占，避免读写归属混乱。
- 共享 state-machine awaitable 家族补齐 `AwaitContext` 注入与统一 `.timeout(...)` 收口，builder / direct 两条路径行为一致。
- 修复 `io_uring` 下普通 awaitable 与 sequence owner 冲突时可能误清 slot、导致 sequence 永久挂起的问题。
- 新增回归测试：
  - `test/T96-state_machine_timeout.cc`
  - `test/T97-state_machine_await_context.cc`
  - `test/T98-sequence_owner_conflict.cc`
  - `test/T99-sequence_duplex_split.cc`
  - `test/T100-sequence_bidirectional_exclusive.cc`
  - `test/T101-sequence_ordinary_conflict.cc`
- `2026-03-21` 远端 Linux `io_uring` fresh 验证结果：
  - 全量测试 `98/98 PASS`
  - `B2/B3` plain TCP loopback：平均约 `119184 QPS`、`232.782 MB/s`、`Errors=0`
  - `B11/B12` `readv/writev` loopback：平均约 `100726 QPS`、`786.923 MB/s`、`Errors=0`

## v3.2.0 非兼容升级

- 旧 `CustomAwaitable` / `CustomSequenceAwaitable` / `addCustom(...)` 扩展模型已移除，不再保留兼容层。
- 自定义组合 IO 统一改为 `SequenceAwaitable + SequenceStep + AwaitableBuilder`。
- 复杂双向协议现在可以直接通过 `AwaitableBuilder<Result>::fromStateMachine(&controller, machine).build()` 接入共享状态机内核。
- 链式 `AwaitableBuilder` 仍保留 `connect(...).recv(...).parse(...).send(...).local(...).finish(...)` 这类用法，但 `build()` 已改为落到共享状态机内核，而不是独立的旧 sequence 执行路径。
- 链式 `AwaitableBuilder` 现在额外支持 `.readv(...) / .writev(...)`，适合 header/body 分段收发这类线性多段 IO。
- 协议解析推荐使用 `AwaitableBuilder::parse(...) + ParseStatus + ByteQueueView`：
  - `ParseStatus::kNeedMore` 会自动重挂最近一个读步骤，不提前唤醒协程
  - `ParseStatus::kContinue` 会继续本地 parse loop，适合单次 `recv` 吃完粘包 backlog
  - `ByteQueueView` 用于累积半包、读取协议头和消费已解析字节
- 状态机动作模型：
  - `MachineSignal::kContinue`：继续本地推进，不等待新的内核事件
  - `MachineSignal::kWaitRead / kWaitReadv / kWaitWrite / kWaitWritev / kWaitConnect`：注册读写或连接完成事件，等待下一次底层完成
  - `MachineSignal::kComplete / kFail`：结束 awaitable
- 真实回归入口：
  - 线性组合：`test/T63-custom_sequence_awaitable.cc`
  - 半包不提前唤醒：`test/T76-sequence_parser_need_more.cc`
  - 粘包本地连读：`test/T77-sequence_parser_coalesced_frames.cc`
  - 状态机读写切换：`test/T86-state_machine_read_write_loop.cc`
  - builder 桥接：`test/T87-awaitable_builder_state_machine_bridge.cc`
  - builder connect 桥接：`test/T90-awaitable_builder_connect_bridge.cc`
  - 自定义状态机 connect：`test/T91-state_machine_connect_action.cc`
  - builder queue 误用拒绝：`test/T92-awaitable_builder_queue_rejected.cc`
  - builder iovec 往返：`test/T94-awaitable_builder_iovec_roundtrip.cc`
  - builder iovec parse 桥接：`test/T95-awaitable_builder_iovec_parse_bridge.cc`

### Awaitable 入门

- 先看 `examples/include/E10-custom_awaitable.cc`：
  这是最小状态机自定义 Awaitable 示例，适合 `connect / read / write / shutdown / handshake` 这类多状态推进场景。
- 先看 `examples/include/E11-builder_protocol.cc`：
  这是最小链式 Builder 协议示例，适合 `recv -> parse -> send` 这类线性协议流，尤其是半包/粘包解析。
- 如果你需要显式 `ops.queue(...)` 或长期持有步骤对象，请直接使用 `SequenceAwaitable + SequenceStep`。
- 更完整的选型说明与入口骨架见 `docs/03-使用指南.md`。

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
  - `E1-SendfileExample` ~ `E5-UdpEcho`，以及 `E10-CustomAwaitable` / `E11-BuilderProtocol` 始终由 `examples/include/*.cc` 生成
  - `E1-SendfileExampleImport` ~ `E11-BuilderProtocolImport` 仅在模块 target 生效时生成
- 测试：`test/T*.cc` 会按文件名直接生成同名 target，例如 `test/T13-async_mutex.cc` -> `T13-async_mutex`
- benchmark：`benchmark/CMakeLists.txt` 明确定义 `B1-ComputeScheduler` 到 `B14-SchedulerInjectedWakeup`

## 性能验证口径（2026-03-21）

- 当前本地 fresh 验证与远端 Linux `io_uring` fresh 验证对应 `v3.4.0` worktree；历史 triplet 对比仍以 `baseline=cde3da1`、`refactored=v3.0.1(59bc155)` 为准
- 单 benchmark 的标准入口是 `scripts/run_single_benchmark_triplet.sh`
- 后端顺序固定为 `kqueue -> epoll -> io_uring`
- `scripts/run_benchmark_triplet.sh` 是单 backend 低层 orchestrator，`scripts/parse_benchmark_triplet.py` 只输出 `baseline | refactored`
- `B5-UdpClient` 只做 smoke/stability 检查，最终 UDP 性能结论以 `B6-Udp` 为准

## 当前已验证的命令（2026-03-21）

验证环境 1：macOS、AppleClang 17、CMake 默认 Release、后端为 kqueue。  
验证环境 2：Ubuntu Linux、`io_uring`、远端保守压测（`nice -n 10` + 绑定 `1-2` 核）。

```bash
python3 -m unittest \
  scripts.tests.test_run_test_matrix \
  scripts.tests.test_run_benchmark_matrix \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet

cmake -S . -B build-awaitable-state-machine -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build-awaitable-state-machine --parallel

bash scripts/run_test_matrix.sh \
  "$PWD/build-awaitable-state-machine" \
  "$PWD/build-awaitable-state-machine/test_matrix_logs_release_2026_03_17"

for name in E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-TaskBasic E5-UdpEcho E10-CustomAwaitable E11-BuilderProtocol; do
  ./build-awaitable-state-machine/bin/$name
done

bash scripts/run_benchmark_matrix.sh \
  --build-dir "$PWD/build-awaitable-state-machine" \
  --log-root "$PWD/build-awaitable-state-machine/benchmark_matrix_logs_release_2026_03_17"
```

实际结果：

- 脚本单测：`28/28` 通过
- 测试：全量 `test matrix` fresh 跑完，日志全部生成，未出现新的 `FAILED` / `Segmentation fault` / `terminate called`
- 示例：
  - `E1-SendfileExample`：PASS
  - `E2-TcpEchoServer`：PASS
  - `E3-TcpClient`：PASS
  - `E4-TaskBasic`：PASS
  - `E5-UdpEcho`：PASS
  - `E10-CustomAwaitable`：PASS
  - `E11-BuilderProtocol`：PASS
- benchmark：
  - `B1` ~ `B14` fresh 跑完并生成当前机器日志
  - `B4/B5-Udp` 已恢复有效收发；`B5` 仍只作为 smoke / stability 检查
  - `B5-UdpClient` 本地 kqueue fresh 结果为 `100000 sent / 99518 received`、loss `0.482%`
  - `B6-Udp` 本地 kqueue fresh 结果为 `200000/200000`、loss `0.00%`、recv throughput `8.86656 MB/s`
- 远端 Linux `io_uring`：
  - 定向回归：`T98/T99/T100/T101` 全部 `PASS`
  - 全量回归：`98/98 PASS`
  - `B1-ComputeScheduler`：空任务约 `1.21M tasks/s`，light 约 `0.99M tasks/s`，heavy 约 `81.7K tasks/s`
  - `B14-SchedulerInjectedWakeup`：约 `1.62M tasks/s`，平均延迟约 `16.3us`
  - `B8-MpscChannel`：持续负载平均约 `20.9M msg/s`
  - `B9-UnsafeChannel`：`recvBatched` 峰值约 `128.7M msg/s`
  - `B2/B3` plain TCP loopback：平均约 `119184 QPS`、`232.782 MB/s`、`Errors=0`
  - `B11/B12` `readv/writev` loopback：平均约 `100726 QPS`、`786.923 MB/s`、`Errors=0`
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

- `galay.kernel` / Runtime / Scheduler / Task / TimerScheduler：先看 `docs/01-架构设计.md`、`docs/02-API参考.md`、`docs/03-使用指南.md`
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
