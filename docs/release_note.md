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
