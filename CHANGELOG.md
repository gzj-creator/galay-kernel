# CHANGELOG

维护说明：
- 未打 tag 的改动先写入 `## [Unreleased]`。
- 需要发版时，从 `Unreleased` 或“自上次 tag 以来”的累计变更整理出新的版本节。
- 版本号遵循 `major/minor/patch` 规则：大改动升主版本，新功能升次版本，修复与非破坏性维护升修订版本。
- 推荐标题格式为 `## [vX.Y.Z] - YYYY-MM-DD`，正文按 `Added` / `Changed` / `Fixed` / `Docs` / `Chore` 归纳。

## [Unreleased]

## [v3.4.5] - 2026-04-22

### Fixed
- 修复 `kqueue` reactor 的 registration token 生命周期与晚到事件校验，避免 fd 关闭或复用后事件误投递到失效 controller。
- 修复 owner 唤醒任务在恢复前被 sibling scheduler 窃取的问题，保证 `SSL` / `Waker` 路径仍回到所属 `IOScheduler` 线程执行。

### Changed
- 扩展 connect fanout、same-scheduler accept/connect、sequence fanout 与 mixed builder connect 压力回归测试，并增强 `B3-tcp_client` 的 connect-only 时延与错误统计输出。

### Chore
- 清理过期的 `docs/plans/` 草案与 `scripts/tests/` 历史脚本，收窄仓库维护面。
