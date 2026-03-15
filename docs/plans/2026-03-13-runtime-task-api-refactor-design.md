# Runtime Task API Refactor Design

## 背景

`v3` 当前的 runtime 内核已经完成一轮企业级分层重构，现有设计文档明确把 `Task<T>`、scheduler API 和更完整的 runtime 扩展留作后续迭代。当前公开协程类型仍然是只支持 `void` 返回的 `Coroutine`，同步入口仍然停留在 `Runtime::start()` / `stop()` + `Scheduler::spawn(...)`。

这带来两个实际问题：

- 无法提供 Tokio 风格的 `blockOn(rootTask)` 返回值语义。
- 上层 API 暴露的是 scheduler 级接口，而不是更稳定的 runtime / handle 抽象。

本轮目标是在不推翻现有 `v3` 内核调度模型的前提下，引入一套更接近 Tokio 的上层 API，并保持与现有代码的兼容迁移路径。

## 参考语义

本设计对齐 Tokio 的用户可见语义，主要参考：

- `Runtime::block_on`: https://docs.rs/tokio/latest/tokio/runtime/struct.Runtime.html
- `Handle`: https://docs.rs/tokio/latest/tokio/runtime/struct.Handle.html
- `spawn`: https://docs.rs/tokio/latest/tokio/task/fn.spawn.html
- `JoinHandle`: https://docs.rs/tokio/latest/tokio/task/struct.JoinHandle.html
- `spawn_blocking`: https://docs.rs/tokio/latest/tokio/task/fn.spawn_blocking.html

这里的“对齐”指对齐用户语义，而不是逐字复制 Tokio 的内部执行模型。特别是 `Runtime::blockOn(...)` 的实现会继续复用当前 `v3` 的 scheduler 内核，而不是额外引入一套专门在调用线程 poll 根任务的 executor。

## 目标

- 新增泛型协程返回类型 `Task<T>`。
- 新增 `Runtime::blockOn(Task<T>) -> T`。
- 新增 `Runtime::spawn(Task<T>) -> JoinHandle<T>`。
- 新增 `Runtime::spawnBlocking(...) -> JoinHandle<R>`。
- 新增 `RuntimeHandle`、`Runtime::handle()`、`RuntimeHandle::current()`、`RuntimeHandle::tryCurrent()`。
- 公开 API 统一使用 camelCase。
- 保留现有 scheduler 内核和 owner-thread 语义，不把本轮扩展做成第二套 runtime。

## 非目标

- 本轮不引入取消 / `AbortHandle`。
- 本轮不引入 `JoinSet`、`select`、`whenAll` 等任务组合 API。
- 本轮不重做底层 scheduler 分层结构。
- 本轮不把 runtime 改造成 Tokio current-thread executor 的内部实现方式。
- 本轮不删除旧的 scheduler API，只做兼容迁移。

## 公开 API

### 核心类型

```cpp
template <typename T = void>
class Task;

template <typename T>
class JoinHandle;

class RuntimeHandle;
```

### Runtime API

```cpp
class Runtime {
public:
    template <typename T>
    T blockOn(Task<T> task);

    template <typename T>
    JoinHandle<T> spawn(Task<T> task);

    template <typename F>
    auto spawnBlocking(F&& func) -> JoinHandle<std::invoke_result_t<F>>;

    RuntimeHandle handle();
};
```

### RuntimeHandle API

```cpp
class RuntimeHandle {
public:
    static RuntimeHandle current();
    static std::optional<RuntimeHandle> tryCurrent();

    template <typename T>
    JoinHandle<T> spawn(Task<T> task) const;

    template <typename F>
    auto spawnBlocking(F&& func) const -> JoinHandle<std::invoke_result_t<F>>;
};
```

### 兼容层

```cpp
using Coroutine = Task<void>;
```

旧的 `Coroutine` 相关调用点先通过别名继续工作，避免本轮把全仓库 awaitable / test / example 一次性全部改掉。

## 架构设计

### 1. `Task<T>` 取代只支持 `void` 的 `Coroutine`

当前 `TaskRef` / `TaskState` / `PromiseType` 的核心思路是对的：scheduler 只依赖轻量任务引用，生命周期由 intrusive refcount 管理。这一层不应被“返回值支持”污染。

因此本轮采用：

- 保留无类型调度核心：`TaskRef`、scheduler queue、wake path 继续只关心任务是否 ready。
- 将结果和异常下沉到泛型任务状态：
  - `TaskStateBase`
  - `TaskState<T>`
  - `Promise<T>`

这样调度路径不需要知道任务返回值类型，结果只在 `Task<T>` / `JoinHandle<T>` / `Runtime::blockOn(...)` 中读取。

### 2. `Coroutine` 兼容迁移

现有大量 awaitable 直接使用 `std::coroutine_handle<Coroutine::promise_type>`。为了降低迁移成本：

- `Coroutine` 先不删除。
- `Coroutine` 通过 `using Coroutine = Task<void>` 或等价兼容层存在。
- 旧代码返回 `Coroutine` 的函数会自然映射到 `Task<void>`。

这意味着本轮可以先新增能力，再逐步把公开示例和文档迁到 `Task<T>`。

### 3. `JoinHandle<T>` 只负责等待结果，不拥有 runtime 生命周期

`JoinHandle<T>` 语义对齐 Tokio：

- `spawn(...)` 返回后，任务会在 runtime 中立即后台执行。
- `JoinHandle<T>` 被 drop 时，任务继续运行，结果被丢弃，相当于 detach。
- `JoinHandle<T>` 负责结果提取与异常重抛，不负责任务调度。

实现上，`JoinHandle<T>` 持有共享任务状态的只读等待权限，而不是新的任务实体。

### 4. `Runtime::blockOn(...)` 对齐语义，不复制 Tokio 内部结构

用户语义：

- 把根任务提交进 runtime。
- 在调用线程阻塞到根任务完成。
- 返回结果或重抛异常。
- 不自动 stop runtime。
- 根任务内部 `spawn(...)` 的后台任务在 `blockOn(...)` 返回后仍可继续运行。

实现推断：

- 根任务仍然作为普通 runtime task 调度到现有 owner scheduler 上。
- `blockOn(...)` 通过一个同步等待器观察根任务完成，而不是在调用线程直接 poll 根任务。

这样可以最大限度复用当前 `v3` scheduler 内核，避免引入第二套“根任务专属执行器”。

### 5. `RuntimeHandle` 是主要公开入口，`thisScheduler()` 不是

对上层用户，稳定抽象应该是 runtime handle，而不是具体 scheduler。

因此：

- 公开 `RuntimeHandle::current()` / `tryCurrent()`。
- `Runtime::handle()` 返回可克隆的 handle。
- `thisScheduler()` 如果需要，保留在内部或底层调试路径，不作为主要公开 API。

### 6. `spawnBlocking(...)` 使用独立 blocking executor

为了贴近 Tokio 语义，`spawnBlocking(...)` 不应直接塞进 `ComputeScheduler`。原因：

- compute scheduler 的语义仍然是协程驱动的计算任务。
- blocking closure 可能长时间占住线程，不应破坏现有 compute scheduler 的公平性与唤醒行为。

本轮建议在 `Runtime` 下引入独立 `BlockingExecutor` / `BlockingPool`：

- 独立线程池或最小线程执行器。
- 接收普通闭包。
- 结果通过 `JoinHandle<R>` 回传。
- 已开始执行的 blocking task 不保证可取消。

## 数据流

### `blockOn(Task<T>)`

1. 调用方传入根 `Task<T>`。
2. `Runtime::blockOn(...)` 为其创建同步完成观察器。
3. 根任务提交到 runtime 的默认 scheduler。
4. 根任务运行并写入 `TaskState<T>` 的结果或异常。
5. 完成观察器唤醒阻塞中的调用线程。
6. `blockOn(...)` 读取结果并返回，或重抛异常。

### `spawn(Task<T>) -> JoinHandle<T>`

1. 任务提交到 runtime。
2. runtime 立即开始后台执行。
3. `JoinHandle<T>` 只保留等待和结果提取能力。
4. 调用方选择等待、轮询完成状态或直接 drop。

### `spawnBlocking(F) -> JoinHandle<R>`

1. 闭包提交到 blocking executor。
2. blocking worker 执行闭包。
3. 结果或异常写入共享状态。
4. `JoinHandle<R>` 在 async 或 sync 路径中提取结果。

## 异常与生命周期

- `Task<T>` / `JoinHandle<T>` / `Runtime::blockOn(...)` 统一使用 `std::exception_ptr` 保存异常。
- 结果读取点直接重抛异常，不做状态码包装。
- `spawnBlocking(...)` 一旦开始执行，不保证可取消。
- runtime shutdown 时，已启动的 blocking task 可能导致等待，这一语义与 Tokio 接近。
- runtime context 通过线程局部状态提供给 `RuntimeHandle::current()` / `tryCurrent()`。
  - `current()` 在没有上下文时失败或 panic。
  - `tryCurrent()` 返回空结果或错误对象，不抛异常。
  - 首版对外更推荐 `tryCurrent()`。

## 兼容与迁移

第一阶段：

- 保持旧的 `Scheduler::spawn(Coroutine)` 和 `Runtime::start()/stop()` 可用。
- 新示例、新测试、新文档优先使用 `Task<T>` 和 `Runtime::blockOn(...)`。
- 旧示例逐步迁移，不强制一次完成。

第二阶段：

- 把高层示例从直接 `scheduler->spawn(...)` 迁到 `Runtime::spawn(...)` / `blockOn(...)`。
- 将文档中的主推荐路径切换到 runtime / handle API。

第三阶段：

- 再评估是否弱化或隐藏直接 scheduler API。

## 验证策略

新增一组专门覆盖新语义的回归测试：

- `T52-runtime_block_on_result.cc`
- `T53-runtime_block_on_exception.cc`
- `T54-runtime_spawn_join_handle.cc`
- `T55-runtime_handle_current.cc`
- `T56-runtime_spawn_blocking.cc`

验证目标：

- `blockOn(Task<int>)` 返回值正确。
- 根任务异常能被同步重抛。
- `spawn(...)` 返回的 `JoinHandle<T>` 能提取结果。
- `JoinHandle<T>` drop 后任务继续执行。
- `RuntimeHandle::current()` / `tryCurrent()` 的上下文语义稳定。
- `spawnBlocking(...)` 能回传结果，且不破坏原有 runtime 测试。

## 结论

本轮推荐按“Tokio 风格公开 API + 现有 `v3` 内核复用”的方向推进：

- 上层新增 `Task<T>` / `JoinHandle<T>` / `RuntimeHandle` / `blockOn` / `spawn` / `spawnBlocking`
- 底层保持 scheduler 内核和 `TaskRef` 路径稳定
- 通过兼容层把旧 `Coroutine` 平滑迁移到 `Task<void>`

这样既能满足 Tokio 风格语义，也不会把现有 `v3` 的企业级 scheduler 内核重构成果推倒重来。

## 实施结果记录（2026-03-13）

- `Task<T>`、`JoinHandle<T>`、`RuntimeHandle`、`Runtime::blockOn(...)`、`Runtime::spawn(...)`、`Runtime::spawnBlocking(...)` 已落地到 `v3` worktree。
- `Coroutine` 兼容层保留，旧的 awaitable / waker / scheduler 路径继续可用。
- `spawnBlocking(...)` 首版落到独立 `BlockingExecutor`，当前实现为每个 blocking 任务分离线程执行，而不是复用 `ComputeScheduler`。
- 新增测试 `T52` 到 `T56` 已覆盖返回值、异常、detach、runtime context 和 blocking task 语义。
- 高层示例 / 测试已开始迁移到新 API，`T25-spawn_simple.cc` 和 `examples/include/E4-coroutine_basic.cc` 现在优先展示 runtime task API。
