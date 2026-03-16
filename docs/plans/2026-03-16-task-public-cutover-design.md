# Task 公开接口切换与 Coroutine 移除设计

## 背景

当前仓库虽然已经引入 `Task<T>`、`JoinHandle<T>`、`Runtime::blockOn(...)`、`Runtime::spawn(...)`、`RuntimeHandle::spawn(...)` 等高层能力，但公开接口仍然处于混合状态：

- `galay-kernel/kernel/Coroutine.h` 同时承载 `Coroutine`、`Task<T>`、`JoinHandle<T>`、`PromiseType`、`TaskPromise<T>`、`WaitResult`、`SpawnAwaitable`
- `galay-kernel/kernel/Scheduler.hpp` 仍以 `spawn(Coroutine)` 作为主提交流程
- 三个后端调度器公开头仍暴露 `Coroutine` 路径
- `test/`、`examples/`、`docs/` 中仍存在 `Coroutine::wait()`、`Coroutine::then(...)`、`spawn(Coroutine)` 用法

用户已经明确确认这次迁移边界：

1. 第一步就要求公开头文件完全不出现 `Coroutine`
2. 不引入新的 `Coroutine<T>` 公开模型
3. 直接把 scheduler 提交原语切到 `TaskRef`
4. `wait/then/spawn` 的公开语义统一收敛到 `Task<T>` 体系
5. 旧写法不保留兼容层，最终要从源码、测试、示例、文档一起移除

## 目标

本轮设计的目标状态：

- 公开协程返回类型只有 `Task<T>`
- 公开异步结果句柄只有 `JoinHandle<T>`
- 公开根任务入口只有 `Runtime::blockOn(Task<T>)`、`Runtime::spawn(Task<T>)`、`RuntimeHandle::spawn(Task<T>)`
- 公开调度器只接受 `TaskRef`，不再接受 `Coroutine`
- `Task<T>` 本身可直接 `co_await`
- 根任务 continuation 能力由 `Task<void>::then(...)` 承接
- 公开源码、示例、测试、文档中不再出现 `Coroutine`、`WaitResult`、`SpawnAwaitable`、`PromiseType`

## 非目标

- 不在本轮引入 `Coroutine<T>` 作为第二套公开抽象
- 不在本轮引入复杂的泛型 task combinator 体系，如 `map/flatMap/andThen`
- 不保留任何仅用于平滑迁移的旧接口别名
- 不把 `TaskRef` 扩展成新的业务层公开抽象

## 方案比较

### 方案 A：只收口公开层，内部继续保留 `Coroutine`

把 `Coroutine` 移到 internal header，公开层只暴露 `Task<T>`。

优点：

- 迁移风险最低
- 可以很快让公开头文件不再出现 `Coroutine`

缺点：

- 调度器内核仍以 `Coroutine` 为中心
- 只是“藏起来”，不是彻底完成模型切换
- 不符合用户这轮希望直接改掉 scheduler 提交原语的要求

### 方案 B：公开层与调度器提交流程一起切到 `TaskRef`

公开 API 仅保留 `Task<T>`，同时把 `Scheduler` 及三后端的任务提交原语统一为 `TaskRef`。

优点：

- 模型最一致
- 第一步就能做到公开头文件彻底不出现 `Coroutine`
- 后续删除旧内部桥接成本最低

缺点：

- 改动面更大
- 需要同步迁移 continuation、awaiter、测试和文档

### 方案 C：引入 `Coroutine<T>` 统一命名

将原 `Coroutine` 泛型化，尝试与 `Task<T>` 合并或并存。

优点：

- 表面上类型系统更“统一”

缺点：

- 会重新制造 `Task<T>` 与 `Coroutine<T>` 双轨
- 用户难以判断哪个才是正式入口
- 对当前 runtime/task 方向是回退

结论：采用方案 B。

## 最终公开边界

合并后的公开协程与执行模型只保留：

- `Task<T>`
- `JoinHandle<T>`
- `Runtime`
- `RuntimeHandle`
- `RuntimeBuilder`
- `TaskRef`

其中：

- `Task<T>` 是唯一协程返回类型，包括 `Task<void>`
- `JoinHandle<T>` 是唯一后台任务观察句柄
- `Runtime` / `RuntimeHandle` 是唯一推荐提交入口
- `TaskRef` 可以存在于 scheduler 抽象中，但不作为业务层文档主角

明确移除的公开符号：

- `Coroutine`
- `PromiseType`
- `WaitResult`
- `SpawnAwaitable`
- `spawn(Coroutine)` 及其重载
- `Coroutine::wait()`
- `Coroutine::then(...)`
- `Scheduler::spawn(Coroutine)`
- `Scheduler::spawnDeferred(Coroutine)`
- `Scheduler::spawnImmidiately(Coroutine)`

## 文件拆分与头文件重组

### 公开头

新增或重组后的公开头建议如下：

- `galay-kernel/kernel/Task.h`
  - `TaskRef`
  - `TaskState`
  - `TaskCompletionState<T>`
  - `Task<T>`
  - `JoinHandle<T>`
  - `TaskPromise<T>`
- `galay-kernel/kernel/Runtime.h`
  - `Runtime`
  - `RuntimeHandle`
  - `RuntimeBuilder`
- `galay-kernel/kernel/Scheduler.hpp`
  - 只声明 task-native 的调度接口

### 内部头

将 runtime/scheduler 需要的低层辅助拆到 detail 层：

- `galay-kernel/kernel/detail/TaskAccess.h`
- `galay-kernel/kernel/detail/TaskRuntime.h`
- 如 continuation bridge 需要单独收敛，可增加 `galay-kernel/kernel/detail/TaskContinuation.h`

### 旧文件处理

`galay-kernel/kernel/Coroutine.h` 不再作为公开头保留。

处理原则：

- 可以重命名并拆分其仍然有效的 `Task` 相关内容
- 不能留下任何公开可见的 `Coroutine` 定义或声明
- 如果还存在过渡逻辑，也只能留在 internal detail 中，且不再以 `Coroutine` 命名

## `wait/then/spawn` 的新公开语义

### `wait()` 替代

旧写法：

```cpp
co_await child.wait();
```

新写法统一为：

```cpp
co_await child;
```

要求：

- `Task<void>` 可被直接 `co_await`，仅等待完成
- `Task<T>` 可被直接 `co_await`，完成后返回 `T`
- 异常通过 `co_await Task<T>` 直接传播

### `then(...)` 替代

为了保留当前根任务 continuation 模型，第一步提供：

- `Task<void>::then(Task<void>) &`
- `Task<void>::then(Task<void>) &&`

设计约束：

- 仅承接当前 `Coroutine::then(...)` 的已使用语义
- continuation 生命周期不依赖临时对象
- 底层继续挂接在 `TaskState::m_then` 上，避免额外包装 task 或额外堆分配

本轮不扩展到泛型结果变换器。

### `spawn(...)` 替代

如仍需要协程内 fire-and-forget 提交能力，可保留 `spawn(Task<T>)` 这一名字，但只接受 `Task<T>`。

语义要求：

- 绑定当前任务所属 runtime/scheduler
- 不再暴露任何 `Coroutine` 版本重载

## 调度器提交路径重构

### 新的 scheduler 主接口

`galay-kernel/kernel/Scheduler.hpp` 调整为只保留：

- `virtual bool schedule(TaskRef task) = 0;`
- `virtual bool scheduleDeferred(TaskRef task) = 0;`
- `virtual bool scheduleImmediately(TaskRef task) = 0;`

说明：

- `schedule(TaskRef)` 成为主提交原语
- `scheduleDeferred(TaskRef)` 承担当前 delayed/deferred ready 提交语义
- `scheduleImmediately(TaskRef)` 取代旧 `spawnImmidiately`，并顺带修正拼写

### 三后端同步改造

以下后端公开头和实现统一切换到 task-native 提交流程：

- `galay-kernel/kernel/KqueueScheduler.h`
- `galay-kernel/kernel/EpollScheduler.h`
- `galay-kernel/kernel/IOUringScheduler.h`

要求：

- 所有 ready / injected / immediate 路径统一以 `TaskRef` 为单位
- 恢复调用统一走 `resume(TaskRef&)`
- 不再以包装对象作为调度边界

### 内部 bridge 改名

当前旧命名：

- `spawnCoroutine(...)`
- `spawnCoroutineImmediately(...)`
- `CoroutineAccess`

应改为 task 语义：

- `scheduleOnScheduler(...)`
- `scheduleImmediatelyOnScheduler(...)`
- `TaskRefAccess`

这样内部命名和公开模型保持一致，不继续泄漏旧抽象。

## Promise 与执行载体统一

### 删除 `PromiseType`

既然 `Coroutine` 不再存在，内部也不应再保留一套只服务旧 `void` 协程的 promise。

统一只保留：

- `TaskPromise<T>`
- `TaskPromise<void>`

### `TaskRef` 作为执行载体

调度器与 runtime 只围绕 `TaskRef` / `TaskState` 工作：

- scheduler 不感知返回值类型
- completion / exception / join 语义只在 `Task<T>` / `JoinHandle<T>` 层处理
- continuation / waiter / runtime 绑定仍挂在 `TaskState` 上

继续复用并保留：

- `TaskState::m_then`
- `TaskState::m_next`

但它们的语义归属于 task 模型，不再归属于 `Coroutine`。

## 测试与迁移要求

### 测试迁移

以下语义必须用新接口重新锁定：

1. `Task<void>` 与 `Task<T>` 的直接 `co_await`
2. `Task<T>` 返回值传播
3. 异常传播
4. `Task<void>::then(...)` 左值/右值语义
5. continuation 生命周期不依赖临时对象
6. `Runtime::blockOn(Task<T>)`
7. `Runtime::spawn(Task<T>) -> JoinHandle<T>`
8. `RuntimeHandle::spawn(Task<T>)`
9. `schedule(TaskRef)` / `scheduleDeferred(TaskRef)` / `scheduleImmediately(TaskRef)`
10. 现有组合式 Awaitable 与复杂自定义 Awaitable 在新任务模型下仍正常工作

### 示例与文档迁移

必须同步完成：

- `examples/` 全部迁到 `Task<T>` 和 runtime 新接口
- `docs/` 中所有 `Coroutine`、`WaitResult`、`SpawnAwaitable` 术语和示例全部移除或改写
- `README` 与 API 文档保持一致

### 回归门槛

最终验收标准：

- 全量 `test/` 通过
- 全量 `examples/` 可构建，关键示例可运行
- 全量 `benchmark/` 可构建并完成既定回归
- `kqueue` / `epoll` / `io_uring` 三端都完成回归验证
- `rg -n "\\bCoroutine\\b|WaitResult|SpawnAwaitable|PromiseType"` 在公开源码、示例、测试、文档中结果为 0

## 迁移顺序

### 阶段 1：类型与头文件切分

- 新建 `Task.h`
- 将 `Task<T>`、`JoinHandle<T>`、`TaskRef`、`TaskPromise<T>` 从旧文件中拆出
- 让公开头彻底不再出现 `Coroutine`

### 阶段 2：scheduler 主提交流程 task-native 化

- 修改 `Scheduler` 抽象
- 修改三后端实现
- 删除 `spawn(Coroutine)` 路径及其桥接

### 阶段 3：await 语义与 continuation 收口

- 让 `Task<T>` 可直接 `co_await`
- 用 `Task<void>::then(...)` 替代旧根任务链式 continuation
- 移除 `wait()` / `SpawnAwaitable`

### 阶段 4：仓库全量迁移

- 迁移 tests
- 迁移 examples
- 迁移 docs
- 删除旧源码与无用测试

### 阶段 5：全量验证与发布准备

- 全量 test/example/benchmark 回归
- 三后端验证
- 清理残留命名和冗余代码
- 为后续版本发布准备变更说明

## 结论

本轮不采用“保留 `Coroutine` 作为内部兼容层”的温和路线，也不引入新的 `Coroutine<T>`。

最终方向是：

- 对外只保留 `Task<T>` 任务模型
- 调度器公开接口直接面向 `TaskRef`
- 旧 `Coroutine` 体系从公开头、实现、测试、示例、文档中一并移除

这样可以一次性把 runtime/task 方向和 scheduler 内核方向统一起来，避免长期维持双轨抽象。
