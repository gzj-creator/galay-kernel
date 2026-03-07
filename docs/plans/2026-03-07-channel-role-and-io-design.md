# Channel Role Split And IO Optimization Design

**背景**

当前 `galay-kernel` 已完成一轮 task-core 轻量化和 `MpscChannel` waiter/wake 正确性修复，`B8-MpscChannel` 不再挂死，关键本地与远端回归已通过。

现状数据说明两件事：

1. `UnsafeChannel` 在单线程 / 同调度器场景下明显更快，适合承担极致低开销通道角色。
2. `MpscChannel` 的主要价值是跨线程单消费者正确性，而不是继续在单线程场景追平 `UnsafeChannel`。

继续给 `MpscChannel` 增加 same-thread 特判，会让内部状态机和可读性继续恶化，收益也不匹配。

**目标**

1. 明确通道分层职责，停止对 `MpscChannel` 做单线程极限性能优化。
2. 让 benchmark 口径与真实使用场景一致，避免继续拿不同语义的路径做不公平比较。
3. 在通道层收敛后，把优化重点转移到 IO 热路径。

**职责划分**

### 1. UnsafeChannel

适用场景：
- 单线程
- 同调度器
- 不需要跨线程 send
- 极低调度/唤醒开销优先

定位：
- 单线程高性能通道
- 用于和 Rust/Go 单线程事件驱动通道路径做对比

### 2. MpscChannel

适用场景：
- 多生产者跨线程发送
- 单消费者协程接收
- 正确性和稳定性优先

定位：
- 跨线程通道
- benchmark 只用于和 Rust/Go 的跨线程 channel 语义做对比
- 不再承担“同线程最快”目标

**benchmark 口径调整**

后续 benchmark 必须拆成两条线：

1. 单线程/同调度器通道对比
- `UnsafeChannel`
- Rust/Go 中对应的同线程事件分发或轻量 channel 路径

2. 跨线程通道对比
- `MpscChannel`
- Rust/Go 中对应的 MPSC / cross-thread channel 路径

禁止再用 `MpscChannel` 的跨线程实现去对标 `UnsafeChannel` 的单线程极限场景，并据此推导调度器或协程层有问题。

**IO 优化主线**

通道职责收敛后，下一阶段主线切到 IO：

1. reactor 事件分发成本
- `epoll_wait` / `kevent` 返回后的事件遍历、对象定位、回调分发

2. completion -> wake 路径厚度
- IO 完成后到任务重新进入 ready 队列的链路是否仍有多余包装

3. socket 热路径
- `read` / `write` / `readv` / `writev`
- awaitable 构造、context 挂接、完成后结果回填

4. backend 批量化能力
- epoll/kqueue 的批量收割
- 多事件 drain 时减少重复调度与分支

5. iouring 准备工作
- 先把 epoll + kqueue 路径打平，再迁移到 io_uring 提交/收割优化

**推荐顺序**

1. 固化通道职责与 benchmark 口径。
2. 补充/整理 benchmark 注释与测试说明，避免误用口径。
3. 采样当前 IO benchmark 基线。
4. 先做 epoll + kqueue 事件分发与 socket awaitable 热路径优化。
5. 基线稳定后再做 Rust/Go 对比。
6. 最后进入 io_uring。

**风险**

1. benchmark 口径不分离时，很容易继续把 channel 语义差异误判成 runtime 调度差异。
2. 如果没有先固定 IO benchmark 基线，后续优化很难判断收益来自 reactor 还是 socket awaitable。
3. epoll/kqueue/uring 三条线同时动会放大回归面，必须坚持分阶段推进。
