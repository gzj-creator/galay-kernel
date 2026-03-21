# Blocking Executor Pool Design

## 背景

当前 `Runtime::spawnBlocking(...)` 通过 `BlockingExecutor::submit(...)` 直接 `std::thread(...).detach()` 执行阻塞任务。这个首版实现语义正确，但会为每个 blocking task 创建一个新的 OS 线程，在短任务高频提交时会放大线程创建、销毁、调度和上下文切换成本。

本轮目标是在不改变 `spawnBlocking(...) -> JoinHandle<T>` 语义的前提下，把底层执行器升级为带上限的弹性 blocking 线程池，并把源码版本元数据先对齐到 `3.4.0`。功能验证通过后，再发布新 tag `v3.4.1`。

## 目标

- 保持 `Runtime::spawnBlocking(...)` / `RuntimeHandle::spawnBlocking(...)` 调用面不变
- 继续使用独立 blocking executor，而不是复用 compute scheduler
- 让 blocking worker 可以复用，而不是每个任务独占一个新线程
- 在空闲 worker 不足时按需扩容，但始终受 `max_workers` 上限约束
- 在低负载时允许多余 worker 超时退出，避免常驻过多空闲线程
- 保证 runtime 析构/停止路径不会丢弃已经被 executor 接收的任务

## 方案

### 1. `BlockingExecutor` 结构

`BlockingExecutor` 改为拥有以下内部状态：

- 任务队列：`std::deque<std::function<void()>>`
- 同步原语：`std::mutex` + `std::condition_variable`
- worker 容器：`std::vector<std::thread>`
- 生命周期状态：`m_stopping`
- 统计状态：`m_worker_count`、`m_idle_workers`
- 配置：`min_workers`、`max_workers`、`keep_alive`

### 2. 提交策略

`submit(...)` 在持锁状态下把任务压入队列，并按以下顺序处理：

1. 如果已有空闲 worker，直接 `notify_one()`
2. 否则如果当前 worker 数量小于 `max_workers`，创建一个新 worker
3. 否则不再扩容，任务留在队列中等待已有 worker 消费

这样可以做到：

- 低负载不提前铺满线程
- 高负载下按需扩容
- 峰值线程数有明确上限

### 3. Worker 生命周期

worker 在循环里执行：

1. 优先从任务队列取任务
2. 若队列为空，则标记自己 idle
3. 若当前 worker 数量高于 `min_workers`，使用 `wait_for(keep_alive)` 等待新任务
4. 若超时且仍然超过 `min_workers``，则自我退出
5. 若 executor 进入 stopping 且队列已空，则退出

这样可以平衡冷启动成本与空闲资源占用。

### 4. 停机语义

`BlockingExecutor` 析构时：

- 设置 `m_stopping = true`
- 唤醒所有 worker
- `join()` 全部 worker 线程

worker 只会在“停止中且队列为空”时退出，因此已接收任务仍会执行完成。这个行为与当前 `spawnBlocking(...)` 已开始执行后不保证取消的语义一致。

## 测试策略

### 回归保留

保留现有 `test/T56-runtime_spawn_blocking.cc`，继续验证：

- `spawnBlocking(...)` 能并发执行阻塞任务
- `JoinHandle<T>` 正确回传结果

### 新增回归

新增一个 `BlockingExecutor` 级别测试，使用小上限配置验证“受限扩容 + 排队”语义：

- 构造一个 `max_workers = 2` 的 executor
- 提交 3 个约 `100ms` 的阻塞任务
- 断言总耗时明显大于单批 `100ms`，说明第 3 个任务被排队而不是立即起新线程
- 同时断言 3 个任务都得到执行

旧实现下，这个测试会在编译期失败，因为旧 `BlockingExecutor` 没有可配置线程池构造和受限扩容行为；实现后该测试应通过。

## 版本与发布

本轮版本线按以下顺序处理：

1. 先把源码版本元数据从 `3.3.0` 对齐到 `3.4.0`
2. 完成 blocking executor 优化与回归验证
3. 在最终验证通过的提交上创建新 tag `v3.4.1`

版本元数据至少同步以下位置：

- 根 `CMakeLists.txt`
- `MODULE.bazel`
- README 当前版本说明

## 风险与约束

- 不引入新的公共 runtime 配置 API，避免这轮需求被放大为 runtime builder 设计重构
- 不修改 `spawnBlocking(...)` 的返回值、异常传递和 runtime context 绑定语义
- 不复用 compute scheduler，避免阻塞任务反向污染计算调度延迟
- 需要注意 `BlockingExecutor` 析构与并发 `submit(...)` 的互斥顺序，避免停机期间竞态
