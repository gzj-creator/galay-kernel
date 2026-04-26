# CHANGELOG

维护说明：
- 未打 tag 的改动先写入 `## [Unreleased]`。
- 需要发版时，从 `Unreleased` 或“自上次 tag 以来”的累计变更整理出新的版本节。
- 版本号遵循 `major/minor/patch` 规则：大改动升主版本，新功能升次版本，修复与非破坏性维护升修订版本。
- 推荐标题格式为 `## [vX.Y.Z] - YYYY-MM-DD`，正文按 `Added` / `Changed` / `Fixed` / `Docs` / `Chore` 归纳。

## [Unreleased]

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
