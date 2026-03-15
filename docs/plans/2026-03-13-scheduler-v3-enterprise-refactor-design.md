# Scheduler V3 企业级重构设计

## 背景

当前 `v3` 已经完成一轮 `Scheduler` 内核增强：

- `wait()` continuation 恢复路径统一经所属 `Scheduler`
- injected wakeup 支持 coalescing
- ready loop 引入 budget
- 在 `kqueue` 下，`B14-SchedulerInjectedWakeup` 的吞吐和延迟已经得到明显修复

但从“企业级可维护 runtime”标准看，当前结构仍有明显问题：

1. 调度策略、唤醒语义、后端 poll 逻辑仍然耦合在 `KqueueScheduler` / `EpollScheduler` / `IOUringScheduler` 内
2. remote enqueue、channel wake、I/O completion、timer wake 的恢复路径尚未完全收敛为统一模型
3. `B8-MpscChannel` 的 tail latency / batch path 仍有波动，说明跨线程唤醒与等待注册语义还不够稳定
4. Linux 两个后端虽然已同步部分优化，但还没有在“统一调度内核 + 后端适配器”结构下完成收敛与实机验证

因此，本轮目标不是继续堆局部补丁，而是做一次受控的内核重构：
在不破坏现有公开 API 风格的前提下，把调度策略、唤醒仲裁和后端实现彻底解耦，并完成本地 `kqueue` 与 Ubuntu 上 `epoll` / `io_uring` 的全量测试和全 benchmark 对比。

## 目标

- 将 `Scheduler` 内核重构为更清晰、可复用、可跨后端对齐的企业级结构
- 统一 `kqueue` / `epoll` / `io_uring` 的调度纪律，而不是维护三份近似但不完全一致的逻辑
- 从根上梳理 remote enqueue、queue-edge wakeup、channel wake、I/O completion 的恢复路径
- 收敛 `MpscChannel` 的等待注册和唤醒边界，重点改善 `B8` 的 latency / batch / cross-scheduler 表现
- 确保 **所有测试通过**
- 确保 **所有 benchmark 都有 baseline vs optimized 对比**
- 在 Ubuntu 实机上完成 Linux 两后端验证，并补齐依赖后继续执行

## 非目标

- 本轮不重构现有公开 API 命名风格
- 本轮不新增 `Task<T>` / `thisScheduler()` 等上层 API 设计
- 本轮不引入 work stealing / priority scheduling / service group 等全新调度模型
- 本轮不将 runtime 改造成 Asio 式共享 `io_context`
- 本轮不因为 benchmark 优化而破坏语义正确性和全量测试稳定性

## 方案选择

### 方案 A：继续在现有调度器类上增量补丁

优点：
- 改动表面较小
- 可以快速继续迭代 `B8`

缺点：
- 结构债务继续累积
- 三个后端之间更难保持长期一致
- 企业级维护成本高

不采用。

### 方案 B：引入模式开关（latency mode / throughput mode）

优点：
- 提供参数化控制能力

缺点：
- 在未先完成结构解耦前，会把复杂度继续堆在现有实现上
- 不能解决后端实现与调度策略耦合的问题

不作为本轮主方案。

### 方案 C：按调度核心 / 唤醒仲裁 / 后端适配 / 等待注册分层重构

优点：
- 能真正把调度策略和平台后端分离
- 更适合企业级维护、调试、扩展和跨平台一致性
- 为后续 API 层演进保留稳定内核

缺点：
- 改动范围较大
- 必须依赖严密的测试和 benchmark 回归约束

本轮采用该方案。

## 目标架构

### Layer 1：`SchedulerCore`

职责：
- 统一管理 ready queue / injected queue / continuation resume
- 统一执行 fairness / budget / burst fast path
- 定义固定事件循环阶段，而不依赖具体后端

不负责：
- 直接处理 `kqueue` / `epoll` / `io_uring` 的系统调用细节
- 直接做底层 wake fd / pipe 操作

### Layer 2：`WakeCoordinator`

职责：
- 统一管理 `sleeping`、`wakeup_pending`、queue edge wakeup、stop wakeup
- 将 remote enqueue 是否需要唤醒的决策从后端类中抽离
- 统一统计 wake 请求、实际 wake、coalesced wake 等指标

预期收益：
- 避免 wakeup 逻辑散落在三套调度器实现中
- 降低 lost wake / duplicate wake 风险

### Layer 3：`BackendReactor`

职责：
- 只负责 I/O 注册、poll、completion 提交
- 向 `SchedulerCore` 交付 ready/completion 结果

实现：
- `KqueueReactor`
- `EpollReactor`
- `IOUringReactor`

原则：
- backend 不再自行决定 remote wake、公平性或 task 恢复策略
- backend 只做“事件收集器”

### Layer 4：等待注册与统一唤醒路径

职责：
- 把 `Waker`、`wait()` continuation、`MpscChannel` consumer registration、I/O completion wake 统一到 owner scheduler 语义
- 所有跨线程恢复最终都收敛为：
  - `enqueueRemote(task, reason)`
  - `WakeCoordinator::requestWake(policy)`
  - owner `SchedulerCore` 在所属线程恢复任务

## 核心数据流

### 1. Remote task / continuation 路径

统一数据流：

- producer / foreign thread 创建或恢复 `TaskRef`
- 通过 `SchedulerCore::enqueueRemote(...)` 注入到 owner scheduler
- `WakeCoordinator` 决定是否发起边沿唤醒或合并唤醒
- owner scheduler loop 在固定阶段 drain injected queue 并恢复任务

### 2. Event loop 固定阶段

统一为：

1. `collectRemote()`
2. `collectBackendCompletions()`
3. `runReady(budget)`
4. `decidePoll()`
5. `pollBackend(timeout)`

意义：
- 调度策略在三套 backend 上保持一致
- I/O 后端退化为事件来源，不再混合业务策略

### 3. `MpscChannel` 路径

重构前问题：
- `send()` 侧和 consumer waiter 之间缺少强语义等待注册结构
- `recv()` / `recvBatch()` 逻辑分散
- 高并发 producer 下容易出现重复 signal 或等待重新 armed 时机不稳定

重构后：
- 引入稳定的 `WaitRegistration`
- consumer 挂起前注册 waiter
- producer 仅负责：
  - `enqueue message`
  - 在“不可消费 -> 可消费”边界发 signal
- 恢复动作通过 owner scheduler 路径统一完成
- `recv()` / `recvBatch()` 共享一套 drain 逻辑

预期收益：
- `B8` latency 更稳
- batch path 不再被过早切碎
- cross-scheduler 场景下的 wake path 更接近 runtime 内核统一语义

## 关键设计点

### 1. Queue edge wakeup 是一等语义

不能仅依赖 `sleeping == true` 决定是否唤醒。

需要支持：
- injected queue 从空变非空时补发一次 wake
- 即使 scheduler 尚未正式进入阻塞态，也能避免错过低延迟窗口
- 同时仍保留 coalescing，避免每个 injected task 都触发一次 syscall

### 2. Fairness 与 latency 并存

`SchedulerCore` 保留：
- ready budget
- injected burst credit
- queue-edge wakeup

原则：
- local self-reschedule 仍受 budget 限制
- pure injected burst 不应被固定 pass 人为切碎
- channel wake / I/O completion 不应再走散乱的专有恢复路径

### 3. 统一统计与可观测性

新增或整理以下统计：
- remote enqueue 次数
- wake 请求次数 / 实际发起次数 / coalesced 次数
- injected drain 次数与大小
- ready 执行数与 pass 次数
- channel signal 次数 / waiter re-arm 次数 / edge transition 次数

这不仅是调优辅助，也是企业级回归判断依据。

## Linux 后端策略

### `epoll`

- 迁移到统一 `SchedulerCore + WakeCoordinator + EpollReactor` 模型
- 保证 remote wake 与 local fairness 语义和 `kqueue` 对齐
- 在 Ubuntu 机器上完成 baseline vs optimized 全 benchmark 对比

### `io_uring`

- 迁移到统一 `SchedulerCore + WakeCoordinator + IOUringReactor` 模型
- 保证 CQE completion 与 remote enqueue 的恢复路径一致
- 在 Ubuntu 机器上完成 baseline vs optimized 全 benchmark 对比
- 若远端缺失 `liburing` / kernel 能力或权限，先补依赖和环境检查，再继续验证

## 错误处理与边界

- `stop()` 必须始终可唤醒 loop，不能被 coalescing 吞掉
- `wait()` 仍保留当前单 waiter 语义，文档和断言继续明确约束
- `MpscChannel` 必须防止：
  - lost wake
  - duplicate wake
  - waiter clear / re-arm 竞态
- Linux 远端验证失败时，需要记录：
  - 缺失依赖
  - 内核能力限制
  - benchmark 无法运行的具体原因
  - 补齐后重新执行结果

## 验证策略

### 本地（macOS / `kqueue`）

必须完成：
- 所有 `test/` 目标通过
- 所有 `benchmark/` 目标均能运行
- baseline vs optimized 完整对比

重点关注：
- `B8-MpscChannel`
- `B14-SchedulerInjectedWakeup`
- `B6-Udp`
- 以及其余 benchmark 是否有意外回退

### Ubuntu（Linux / `epoll` + `io_uring`）

必须完成：
- `epoll`：全量 test + 全 benchmark，对比 baseline vs optimized
- `io_uring`：全量 test + 全 benchmark，对比 baseline vs optimized

远端机器：
- Host: `140.143.142.251`
- User: `ubuntu`

凭据按本轮会话用户授权使用。

## 验收条件

本轮只有在以下条件全部满足时才视为完成：

1. `Scheduler` 内核分层重构落地
2. `kqueue` / `epoll` / `io_uring` 三后端都接入新模型
3. 所有测试目标全部通过
4. 所有 benchmark 都完成 baseline vs optimized 对比
5. 本地 `kqueue` 与 Ubuntu 上 `epoll` / `io_uring` 都有结果文档
6. 明确列出：
   - 提升项
   - 持平项
   - 回退项
   - 回退原因与接受判断

## 结论

本轮应将 `Scheduler` 从“调优中的平台特定实现”推进为“企业级、结构清晰、跨平台一致、可观测、可验证”的 runtime 内核。

完成后，`v3` 不只是解决当前 `B14/B8` 的局部指标问题，而是为后续 `Task<T>`、scheduler API、runtime 功能扩展提供长期稳定的内核基础。
