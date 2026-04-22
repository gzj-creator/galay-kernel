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
