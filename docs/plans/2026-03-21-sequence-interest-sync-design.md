# Sequence Interest Sync 设计

## 背景

当前 `sequence` 热路径在 reactor 侧存在两类重复成本：

- `SequenceAwaitable` 每次推进后，reactor 都会重新扫描 owner，重新计算当前真正等待的方向。
- `kqueue` 后端每次都会对 read/write 方向做整包式 `EV_ADD | EV_ONESHOT`，即使本轮只需要重新 arm 一个方向。

这在 `ws`、`wss`、`h2` 这类高频 `sequence` 推进路径上会被放大，热点会落在：

- `suspendSequenceAwaitable`
- `KqueueReactor::addSequence`
- `collectSequenceEvents`

## 目标

- 不改 `AwaitableBuilder` 和协议层 API。
- 不回到协议专用 awaitable。
- 保持“同一 controller 上多个 sequence 并发共存”的现有语义。
- 将 `sequence` 当前 interest 收敛为 runtime 内部状态，减少重复扫描和无效 re-arm。

## 非目标

- 不修改 `SequenceOwnerDomain` 语义。
- 不重写 `StateMachineAwaitable` 流程。
- 不引入新的对外公开接口。

## 方案对比

### 方案 A：每次继续全量扫描并全量 re-arm

优点：

- 实现最少

缺点：

- 热点不变
- kqueue 仍有明显固定成本

### 方案 B：在 awaitable 对象上缓存当前 active slot/type

优点：

- 局部性能更好

缺点：

- backend 细节泄漏进 awaitable
- 接口和状态都更膨胀

### 方案 C：在 controller 上维护 sequence interest/armed 状态

优点：

- 不改上层接口
- `kqueue` 可以做差量 arm/disarm
- `epoll` 可以复用已有 `m_registered_events` 做统一收敛

缺点：

- 需要同步调整 `Awaitable.h`、`KqueueReactor.cc`、`EpollReactor.cc`

## 结论

采用方案 C。

## 设计

### 1. 在 `IOController` 内部维护 sequence runtime 状态

增加两个内部字段：

- `m_sequence_interest_mask`：当前 sequence 真正等待的方向
- `m_sequence_armed_mask`：当前 backend 已经 arm 的方向

这两个字段仅服务 runtime 内部优化，不对外暴露。

### 2. 把 sequence interest 计算收敛到公共 helper

在 `Awaitable.h` 的 `detail` 命名空间内新增轻量 helper，用于：

- 将 `IOEventType` 映射为 read/write slot mask
- 从 controller 上的 sequence owner 收集当前 interest
- 在 sequence 推进一步后同步 controller 的 cached interest

### 3. `kqueue` 改成差量 arm/disarm

`KqueueReactor` 不再每次根据 owner 全量拼装 read/write 事件，而是：

1. 事件触发后先把对应 slot 从 `armed_mask` 中移除
2. `owner->onActiveEvent(...)` 推进一步
3. 根据新的 `interest_mask` 计算需要新增和删除的 filter
4. 只对变化的方向调用 `kevent`

### 4. `epoll` 复用统一 interest helper

`epoll` 仍然依赖 `m_registered_events` 做最终去重，但 `sequence` 自身的 interest 计算统一改为 controller cache，不再在多个函数里重复 collect。

## 风险与控制

- 风险：同一 controller 上 read/write split owner 的语义被破坏
  - 控制：保留现有 owner 判定逻辑，只优化 interest 同步和 backend arm 策略
- 风险：kqueue 删除不存在的 filter 触发错误
  - 控制：仅对 `armed_mask` 中已 arm 的方向发 `EV_DELETE`
- 风险：状态机 awaitable 在 `prepareForSubmit()` 前没有 active task
  - 控制：interest 同步放在 `prepareForSubmit()` 之后

## 验证

先做 helper 级测试，再做后端和集成验证：

```bash
cmake --build build --target T103-sequence_interest_sync --parallel
./build/test/T103-sequence_interest_sync
```

之后回归：

```bash
cmake --build build --target T39-awaitable_hot_path_helpers T98-sequence_owner_conflict T99-sequence_duplex_split T100-sequence_bidirectional_exclusive --parallel
```

需要时继续补 `galay-http` 的 `ws/wss/h2` 压测对比，确认热点下降且功能不回退。
