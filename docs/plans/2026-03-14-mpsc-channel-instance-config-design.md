# MpscChannel Instance Config Design

**Context**

`MpscChannel` 当前把 `DEFAULT_BATCH_SIZE` 和 `SINGLE_RECV_PREFETCH_LIMIT` 写成类内常量。上一轮为收回 `B8-MpscChannel` latency 引入了 recv-side prefetch，但用户希望这两个值能按实例配置，而不是固定在类型级别。

**Decision**

采用实例级构造配置，而不是编译期宏或模板参数：

- `MpscChannel(size_t defaultBatchSize = 1024, size_t singleRecvPrefetchLimit = 16)`
- `recvBatch()` / `tryRecvBatch()` 提供无参重载，默认使用实例配置
- 保留带 `maxCount` 参数的重载，用于单次覆盖

**Why**

- 无参用法保持简洁，现有调用点基本不用改
- 比编译期宏更细粒度，适合 benchmark 和不同 channel 实例按需调参
- 比模板参数更容易使用，也不引入类型膨胀

**Implementation Notes**

- `singleRecvPrefetchLimit` 改成运行时值后，prefetch buffer 需要从固定 `std::array` 改成动态 `std::vector`
- `size()` 语义保持不变，仍表示“尚未交付给消费者的总消息数”，包含已预取但尚未返回给调用方的数据
- `recvBatch()` / `tryRecvBatch()` 的无参版本使用实例默认 batch size

**Verification**

- 新增测试覆盖构造函数配置的默认 batch / prefetch 行为
- 保持 `T14`、`T37`、`T38`、`T58` 通过
