# MpscChannel Waiter/Wake 修复设计

**背景**

当前 `MpscChannel` 在 batch receive 压测下会出现消费者永久挂起，而队列中仍残留大量消息。现象已经由 `B8-MpscChannel` 和临时 batch-stress 复现：生产者完成后，消费者仍停在 `recvBatch()`，`channel.size()` 持续大于 0。

**已确认问题**

1. waiter 发布协议不安全：消费者通过共享可变 `Waker m_waker` + `m_waiting` 向生产者发布 waiter，生产者跨线程直接读取 `m_waker` 并 `wakeUp()`，存在发布时序缺口和数据竞争。
2. `m_size` 与队列真实可见性解耦：`await_ready()` 只看 `m_size`，并不能保证 `ConcurrentQueue` 中的数据此刻一定可被消费；同时当前 send/dequeue 的记账顺序也不够稳。

**候选方案**

1. 保留 `m_waker + m_waiting`，继续加 probe/spin 补丁。
代价是状态机继续隐式化，正确性难证明，benchmark 仍不可信。

2. 改为原子发布 waiter 句柄，并让 awaitable 直接用真实 dequeue 结果决定 ready/suspend。
优点是状态机清晰：`空 -> 发布 waiter -> 再次尝试消费 -> 成功则取消挂起，失败则真正挂起`；生产者只对原子 waiter 做 `exchange(nullptr)`，跨线程路径明确。

3. 直接替换为 `BlockingConcurrentQueue`/信号量型实现。
正确性容易做，但改动面较大，会改变现有通道内部结构，也不利于继续与当前 scheduler/waker 架构对齐。

**采用方案**

采用方案 2。

**设计要点**

1. `MpscChannel` 内部将 `m_waker + m_waiting` 收敛为原子 waiter 句柄，例如 `std::atomic<void*> m_waiter`。
2. 生产者发送完成后仅做一件事：如果存在 waiter，就 `exchange(nullptr)` 取走并唤醒对应协程。
3. `recv()/recvBatch()` 的 `await_ready()` 改为直接尝试消费并缓存结果，而不是只看 `m_size`。
4. `await_suspend()` 先发布 waiter，再做一次真实消费；若此时已经拿到数据，则尝试撤销 waiter 并避免挂起；若 waiter 已被生产者取走，则允许正常挂起，等待已发布的 wake 生效。
5. `await_resume()` 需要清理可能残留的 waiter，用于覆盖 timeout/外部唤醒等路径，避免悬空 waiter。
6. `m_size` 的记账顺序与读路径一起收敛，避免消费者先 dequeue、生产者后记账造成的不稳定状态。

**验证策略**

1. 先新增一个会稳定复现的大消息量 batch-stress 测试，覆盖“生产者完成但消费者卡住”的路径。
2. 修复后回归 `T37`、新 stress test、`B8-MpscChannel`、`B9-UnsafeChannel`。
3. 再跑既有关键回归和网络 benchmark，确认修复没有把之前的 task-core 优化回退掉。
