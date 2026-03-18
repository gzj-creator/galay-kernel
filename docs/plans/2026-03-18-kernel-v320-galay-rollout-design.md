# Kernel v3.2.0 Galay Multi-Repo Rollout Design

## 背景

`galay-kernel` 当前已经发布到 `v3.2.0`。这一版把 Awaitable 扩展模型继续收口，README 已明确写明旧 `CustomAwaitable` / `CustomSequenceAwaitable` / `addCustom(...)` 扩展路径已移除或不再作为兼容承诺保留；新的正式扩展面转向 `Awaitable.h` 中的状态机与 builder 风格接口。

与此同时，`galay-ssl` 已经完成对新内核 Awaitable 模型的适配，成为当前最接近“标准参考实现”的下游仓库。但其他多个 `galay-*` 仓库仍大量停留在旧的：

- `CustomAwaitable`
- `addTask(...)`
- `m_cursor`
- 直接窥探底层任务队列

这导致它们与 `galay-kernel v3.2.0` 的设计方向脱节，也削弱了后续版本发布时的可维护性、可读性与性能叙述的一致性。

用户要求：

1. 参考 `galay-ssl`，让其他 `galay-*` 仓库全面适配 `galay-kernel`
2. 每个仓库都提供对应压测结果与性能提升说明
3. 全面回归测试
4. 若无问题，每个实际改动仓库中文提交 git、打新 tag、发布 release
5. 可以起多个 agent 并行推进

## 目标

- 让所有实际发生改动的 `galay-*` 仓库完成 `galay-kernel v3.2.0` 的 Awaitable 模型适配
- 统一参考 `galay-ssl` 当前写法，消灭旧 `CustomAwaitable + addTask(...) + m_cursor` 主路径
- 为每个实际改动仓库补足 fresh 回归测试与 fresh benchmark / pressure 记录
- 同步 README、主干文档、版本号、release notes
- 对每个实际改动仓库独立完成中文提交、annotated tag、GitHub Release

## 非目标

- 不对未发生实际改动的仓库强行大版本发布
- 不在没有 fresh 压测证据时虚报量化性能提升
- 不为了追求“统一外观”而机械重写所有复杂状态机
- 不引入新的临时兼容层来延缓收口

## 当前现状判断

### 已适配参考仓库

- `galay-ssl`

说明：

- 当前仓库中已经出现新的 `SslAwaitableCore` 路径，可作为迁移参考
- 但该仓库本身仍需要作为“标准样板”复核文档、压测与版本说明

### 高优先级待迁移仓库

- `galay-http`
- `galay-rpc`

说明：

- 两者对顺序 IO Awaitable 组合依赖最重
- 目前仍存在大量 `CustomAwaitable`、`addTask(...)`、`m_cursor` 直接使用

### 第二优先级待迁移仓库

- `galay-mysql`
- `galay-mongo`
- `galay-redis`

说明：

- 三者都包含明显的协议链式状态机
- 需要把 connect / auth / query / pipeline / recv-loop 等核心路径收口到新的状态机写法

### 兼容验证优先仓库

- `galay-etcd`
- `galay-mcp`

说明：

- 这两个仓库可能只需要少量兼容修补与文档、压测同步
- 只有真实失配时才做代码层迁移

### 默认只做验证的仓库

- `galay-utils`

说明：

- 当前未观察到直接的 Awaitable 迁移点
- 若执行过程中发现必须改代码，再纳入发布

## 方案比较

### 方案 A：逐仓最小兼容修补

优点：

- 实施最快
- 短期阻塞最少

缺点：

- 代码风格继续分裂
- 这次大版本发布说服力弱
- 性能与可读性收益难以统一叙述

### 方案 B：以 `galay-ssl` 为参考的分波次全面适配

优点：

- 迁移基线明确
- 适合多个 agent 并行
- 更适合做统一回归、统一压测、统一 release 叙事

缺点：

- 前期需要更强的设计与验证纪律

### 方案 C：先在各仓库本地包一层临时兼容封装

优点：

- 改动风险分散

缺点：

- 会引入新的临时层
- 和这次“大版本收口”目标冲突

结论：采用方案 B。

## 执行波次

### 第一波：协议面重仓库

- `galay-http`
- `galay-rpc`

目标：

- 先解决最重的 Awaitable 组合使用点
- 确立除 `galay-ssl` 之外的第二批参考写法

### 第二波：客户端协议仓库

- `galay-mysql`
- `galay-mongo`
- `galay-redis`

目标：

- 统一连接、认证、查询、pipeline 的链式 IO 写法
- 结合真实服务环境完成压测

### 第三波：兼容验证与最小修补

- `galay-etcd`
- `galay-mcp`

目标：

- 先验证是否已经兼容 `v3.2.0`
- 只有出现真实失配再做最小代码修补

## 迁移规则

### 代码迁移规则

- 迁移基线统一参考 `galay-ssl`
- 不再继续扩散旧 `CustomAwaitable + addTask(...) + m_cursor` 主路径
- 协议层仓库优先改造顺序状态机核心，再由公开 Awaitable 外壳承接
- 文档中不得继续把旧 Awaitable 模型写成推荐用法
- 若某个复杂状态机必须保留部分低层细节，至少要把公开写法和主路径收敛到新模型

### 仓库级行为要求

- `galay-http` / `galay-rpc`：优先消灭协议收发链路里的旧队列游标模式
- `galay-mysql` / `galay-mongo` / `galay-redis`：重点改造 connect/auth/query/pipeline 路径
- `galay-etcd` / `galay-mcp`：兼容验证优先，最小修补为辅

## 回归标准

每个仓库至少满足：

1. 新增一个最小回归点，直接命中本次 Awaitable 迁移主链路
2. 跑仓库现有核心测试集，不只跑 smoke
3. 外部依赖型仓库在真实服务环境下跑通关键 integration 测试

当前已知服务环境：

- MySQL：本地
- Redis：本地
- etcd：本地
- Mongo：`140.143.142.251`

只有 fresh 回归通过的仓库，才允许进入 tag / release。

## 压测口径

- 只使用仓库内已存在且 README / `docs/05-性能测试.md` 已记录的 benchmark / pressure target
- 每个实际改动仓库至少保存一组 fresh 原始输出
- 若能得到迁移前同机基线，则在 release 中写量化提升
- 若不能得到同机基线，则只写 fresh 结果与热路径收敛说明，不虚报提升百分比
- `Mongo` 的压测结果必须标注远端服务与网络环境影响

## 版本与发布规则

- 所有实际发生改动并完成验证的仓库统一做大版本升级
- 每个仓库独立同步：
  - 版本号
  - 中文 git 提交
  - annotated tag
  - GitHub Release
- 每个 release 至少包含：
  - 适配了哪些 `galay-kernel v3.2.0` 新用法
  - 替换了哪些旧 Awaitable 路径
  - 本次回归测试范围
  - 本次压测命令与结果摘要
  - 升级注意事项

## 执行架构

- 总控仓库使用 `galay-kernel`
- 总控分支位于独立 worktree 中
- 每个目标仓库建立独立 worktree，统一使用 `kernel-v320-adapt` 分支名
- 每个仓库执行顺序统一：
  1. failing test
  2. Awaitable 适配
  3. fresh 回归
  4. fresh 压测
  5. 文档与版本号更新
  6. 中文提交
  7. tag
  8. release

## 多 Agent 协作策略

- 第一波与第二波内部可以多 agent 并行
- 但波次之间必须收口，不在第一波未稳定时大规模推进第二波
- 每个 agent 只负责一个仓库，避免跨仓库互相污染
- 总控层统一维护结果汇总与发布台账

## 风险控制

- 避开现有脏工作区与未跟踪产物
- 只在 worktree 中改动
- 没有 fresh 证据，不得声称完成
- 没有实际改动，不得强行发版
- 压测结论必须区分“同机可比”与“仅本次 fresh 结果”

## 最终交付物

- 一份总设计文档
- 一份总实施计划
- 每个实际改动仓库的代码迁移结果
- 每个实际改动仓库的 fresh 回归记录
- 每个实际改动仓库的 fresh 压测记录
- 每个实际改动仓库的 README / docs / release notes
- 每个实际改动仓库的中文提交、tag、release
- 一份最终总汇总，记录所有仓库的新版本与验证状态
