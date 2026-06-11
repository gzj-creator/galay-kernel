# CHANGELOG

维护说明：
- 未打 tag 的改动先写入 `## [Unreleased]`。
- 打新 tag 时，将 `Unreleased` 中累计变更整理成对应版本节，并保留空的 `Unreleased` 节。
- 版本号遵循语义化版本：破坏性接口变更升主版本，新增能力升次版本，修复与维护升修订版本。
- 记录内容以可读变更摘要为主，避免机械罗列完整 diff。

## [Unreleased]

## [v5.2.0] - 2026-06-11

### Changed
- 将 `galay-utils` 最低依赖版本从 `3.1.0` 提升到 `3.2.0`，同步更新 CMake package config 模板中的版本约束与错误提示。

### Release
- 将 CMake 与 Bazel 版本元数据提升到 `v5.2.0`，与本次 minor tag 对齐。

## [v5.1.1] - 2026-06-09

### Changed
- `Runtime::blockOn()`、`Runtime::spawn()`、`Runtime::spawnBlocking()` 与 `RuntimeHandle` 相关提交接口改为通过 `std::expected` 返回错误，不再在 runtime API 边界使用 `throw` / `try` / `catch` 传播失败。
- 新增 `RuntimeError`、`TaskResultError` 与 `BlockingExecutorError` 错误对象，保留错误码并通过无分配的 `message()` 将 code 映射为可读错误原因。
- `JoinHandle::wait()` / `join()` 与 `co_await Task<T>` 的结果消费路径改为返回 `std::expected`，重复消费、结果缺失、调度失败等错误通过返回值继续向外传播。

### Tests
- 新增 runtime expected 源码边界测试，锁定 `runtime`、`task`、`blocking_executor` 调用链不再引入 `throw`、`try`、`catch`、`@throws` 或 `std::runtime_error`。
- 更新任务、spawn、join、await、blocking executor 相关测试与 include/import 示例，覆盖新的 expected 返回值 API 与 `message()` 错误原因输出。

### Release
- 将 CMake 与 Bazel 版本元数据提升到 `v5.1.1`，与本次 patch tag 对齐。

## [v5.1.0] - 2026-06-07

### Changed
- 移除 `galay-kernel` 本地 `common/bytes.h` / `bytes.cc`，统一复用 `galay-utils/cache/bytes.hpp` 中的 `Bytes`、`ByteMetaData` 与字节内存辅助函数。
- 移除 `galay-kernel/common/queue_view.h` 本地实现，协议解析示例与测试改为直接包含 `galay-utils/cache/byte_queue_view.hpp`。
- 移除 `galay-kernel` 本地 `RingBuffer` 实现，`galay-kernel/common/buffer.h` 通过 using 保留 `galay::kernel::RingBuffer` 入口并复用 `galay-utils/cache/ring_buffer.hpp`。
- `galay-kernel` CMake 与 package config 声明依赖 `galay-utils >= 3.1.0`，优先使用已安装的 `galay::galay-utils` 目标，开发构建仍支持 `GALAY_UTILS_INCLUDE_DIR`。

### Docs
- 更新 API、使用指南与环形缓冲区文档，说明 `Bytes`、`ByteQueueView` 和 `RingBuffer` 已由 `galay-utils` 提供。

### Release
- 将 CMake 与 Bazel 版本元数据提升到 `v5.1.0`。

## [v5.0.0] - 2026-05-20

### Changed
- 将日志注册从全局 `LoggerRegistry` 改为按库隔离的 `LoggerSlot<Tag>`，下游库通过各自命名空间的 `log::set()` / `log::get()` 独立启用日志。
- 移除旧 `GALAY_LOG_*` 全局宏入口，新增 `GALAY_LOG_WITH_LOGGER(getter, level, ...)` 作为下游库定义日志宏的公共基础。
- 新增 `GALAY_LOG_ENABLED(getter, level)` 与 `GALAY_KERNEL_LOG_ENABLED(level)`，供调用点在构造昂贵日志参数前先判断是否会实际写日志。
- `galay-kernel` 自身日志改为 `galay::kernel::log::set()` / `galay::kernel::log::get()` 与 `GALAY_KERNEL_LOG_*` 宏。

### Fixed
- 新增 `t122_logger_slot` 回归测试，验证不同 logger 槽位互相隔离，并验证 logger 为空或日志级别被过滤时不会求值格式化参数。

### Release
- 本次移除全局日志注册兼容层，属于破坏性公共接口变更，版本提升到 `v5.0.0`。

## [v4.0.2] - 2026-05-20

### Added
- 新增 `BaseLogger` 虚基类与 `LoggerRegistry` 全局注册中心（`common/logger.h`），支持用户注入自定义日志实现。
- 新增 `GALAY_LOG_*` 宏族（`common/log_macro.h`），未设置 logger 时零开销（仅 atomic load + null check）。
- 支持 `LogLevel` 五级过滤（kTrace/kDebug/kInfo/kWarn/kError），通过 `minLevel()` 在格式化前截断低级别消息。
- 导出 `logger.h` 到 C++23 module `galay.kernel`。

### Docs
- 为全部 68 个源文件（common/kernel/async/concurrency）添加完整中文 Doxygen 注释，覆盖文件级、类级和方法级文档。

## [v4.0.1] - 2026-05-18

### Fixed
- `TaskResultStorageTraits` 按编译期内联/堆存储策略拆分销毁与取值路径，避免 `Task<std::string>` 在 GCC 下触发 `-Wfree-nonheap-object`。
- 新增 `t121_taskresult_storage` 回归测试，在 GNU 编译器下将该告警提升为错误，锁定内联结果存储行为。

### Changed
- 将安装导出的 CMake targets 文件改为 `galayKernelConfigTargets.cmake`，同步 package config 的 include 路径。
- Release 安装现在生成 `galayKernelConfigTargets-release.cmake`，与新的驼峰导出文件命名保持一致。
- 将 CMake 与 Bazel project 版本提升到 `4.0.1`，对齐本次发布 tag。


## [v4.0.0] - 2026-04-29

### Changed
- 统一源码、头文件、测试、示例与 benchmark 文件命名为 `lower_snake_case`，编号前缀同步使用 `t<number>_`、`e<number>_` 与 `b<number>_` 风格。
- 同步更新构建脚本、模块入口、示例、测试、文档与脚本中的文件路径引用。
- 将项目内头文件包含调整为基于公开 include 根或模块根的非相对路径。

### Release
- 按大版本发布要求提升版本到 `v4.0.0`。

## [v3.4.6] - 2026-04-26

### Fixed
- 修复 `io_uring` sequence socket 进度推进问题：`READV` 改为基于 `POLLIN` + 非阻塞读取驱动，避免已就绪字节在 staged sequence 中丢失，保证 owner 在立即完成路径下及时唤醒。
- 修复 `IOController` move 后转移状态被 moved-from controller 误失效的问题，并补强 ready recv 聚合消费与 `ENOBUFS`/瞬时错误处理，避免接收结果丢失或提前报错。
- 修正多组调度器/通道测试中的统计常量输出，避免压力回归日志计数误报。

### Docs
- 新增 `AGENTS.md` 仓库目录结构与构建约束模板，统一目录职责、对外接口注释、测试/基准、版本对齐与命名风格要求。

## [v3.4.5] - 2026-04-22

### Fixed
- 修复 `kqueue` reactor 的 registration token 生命周期与晚到事件校验，避免 fd 关闭或复用后事件误投递到失效 controller。
- 修复 owner 唤醒任务在恢复前被 sibling scheduler 窃取的问题，保证 `SSL` / `Waker` 路径仍回到所属 `IOScheduler` 线程执行。

### Changed
- 扩展 connect fanout、same-scheduler accept/connect、sequence fanout 与 mixed builder connect 压力回归测试，并增强 `B3-tcp_client` 的 connect-only 时延与错误统计输出。

### Chore
- 清理过期的 `docs/plans/` 草案与 `scripts/tests/` 历史脚本，收窄仓库维护面。
