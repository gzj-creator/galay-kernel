# CHANGELOG

维护说明：
- 未打 tag 的改动先写入 `

## [Unreleased]

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
