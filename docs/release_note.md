# Release Note

按时间顺序追加版本记录，避免覆盖历史发布说明。

## v3.4.5 - 2026-04-22

- 版本级别：小版本（patch）
- Git 提交消息：`chore: 发布 v3.4.5`
- Git Tag：`v3.4.5`
- 自述摘要：
  - 修复 `kqueue` reactor 的 registration token 生命周期与事件校验链路，避免 fd 复用后晚到事件命中失效 controller。
  - 修复 owner 唤醒任务被 sibling scheduler 窃取导致的跨线程恢复问题，保证 `SSL` / `Waker` 路径在所属 `IOScheduler` 线程恢复。
  - 扩展 connect 并发回归测试与 `B3-tcp_client` 连接时延统计，并清理过期的计划文档和脚本测试资产。

## v3.4.6 - 2026-04-26

- 版本级别：小版本（patch）
- Git 提交消息：`chore: 发布 v3.4.6`
- Git Tag：`v3.4.6`
- 自述摘要：
  - 修复 `io_uring` sequence socket 的 `READV` 进度推进链路，改为以 `POLLIN` 配合非阻塞读取驱动 staged sequence，避免已就绪数据被漏消费，并确保立即完成路径能够及时唤醒 owner。
  - 修复 `IOController` move 后状态转移与 ready recv 聚合消费细节，补强 `ENOBUFS`、瞬时错误与多段接收结果处理，避免接收结果丢失或错误提前上浮。
  - 修正多组调度器与通道测试的统计常量输出，并补充 `AGENTS.md` 仓库约束文档以统一目录、构建、测试与版本对齐规范。

## v4.0.0 - 2026-04-29

- 版本级别：大版本（major）
- Git 提交消息：`refactor: 统一源码文件命名规范`
- Git Tag：`v4.0.0`
- 自述摘要：
  - 将源码、头文件、测试、示例与 benchmark 文件统一重命名为 lower_snake_case，编号前缀同步改为小写下划线形式。
  - 同步更新 CMake/Bazel 构建描述、模块入口、README/docs、脚本和所有项目内 include 路径引用。
  - 移除项目内相对 include，统一使用基于公开 include 根或模块根的非相对路径。

## v4.0.1 - 2026-05-18

- 版本级别：小版本（patch）
- Git 提交消息：`fix: 修复 Task 结果存储告警并统一导出命名`
- Git Tag：`v4.0.1`
- 自述摘要：
  - 修复 `TaskResultStorageTraits` 在内联结果类型上仍实例化堆释放路径的问题，避免 `Task<std::string>` 在 GCC 优化内联后触发 `-Wfree-nonheap-object`。
  - 新增 `t121_taskresult_storage` 回归测试，在 GNU 编译器下把该告警提升为错误，并覆盖 `Task<std::string>` 的返回与消费路径。
  - 将安装导出的 CMake targets 文件改为 `galayKernelConfigTargets.cmake`，Release 安装生成 `galayKernelConfigTargets-release.cmake`，并将 CMake/Bazel 版本元数据提升到 `4.0.1`。

## v4.0.2 - 2026-05-20

- 版本级别：中版本（minor）
- Git 提交消息：`feat: 新增 BaseLogger 日志抽象接口并为所有源文件添加中文 Doxygen 注释`
- Git Tag：`v4.0.2`
- 自述摘要：
  - 新增 `BaseLogger` 虚基类、`LoggerRegistry` 全局注册中心和 `GALAY_LOG_*` 宏族，提供零开销可插拔日志基础设施。未设置 logger 时仅执行 atomic load + null check，不进入格式化。
  - 支持 `LogLevel` 五级过滤（kTrace/kDebug/kInfo/kWarn/kError），低级别消息在 `std::format` 前被截断。
  - 为全部 68 个源文件（common/kernel/async/concurrency）添加完整中文 Doxygen 注释，覆盖文件级、类级和方法级文档。
  - 导出 `logger.h` 到 C++23 module `galay.kernel`。
