# Runtime/Task 全量切换与旧接口彻底移除设计

## 背景

当前仓库已经引入 `Runtime`、`Task<T>`、`JoinHandle<T>`、`RuntimeHandle`、`blockOn(...)`、`spawn(...)`、`spawnBlocking(...)` 等高层接口，但整个仓库仍处于“双轨状态”：

- `examples/` 大部分仍直接使用 `Coroutine + *Scheduler`
- `test/` 与 `benchmark/` 里大量 target 仍直连 `Coroutine`、`IOScheduler`、`ComputeScheduler`
- `Runtime` 自身仍直接持有 `ComputeScheduler` / `IOScheduler`
- `TcpSocket` / `UdpSocket` / `Awaitable` 仍通过旧 scheduler 类型注册 IO
- 主干文档仍保留旧接口作为真实推荐路径的一部分

用户已明确要求：

1. 全部 example 迁移到最新接口
2. 主干文档同步迁移，不再推荐旧接口
3. 旧接口源码不保留兼容层，直接删除
4. `test/`、`benchmark/` 也一并迁移
5. 全量验证通过后，重新提交 `v3.0.1` tag 与 release

## 目标

本轮合并后的目标状态：

- 对外唯一执行入口为 `Runtime`、`RuntimeBuilder`、`RuntimeHandle`、`Task<T>`、`JoinHandle<T>`、`sleep(...)` 与各类 async primitive / socket / file 类型
- `Coroutine`、`Scheduler`、`IOScheduler`、`ComputeScheduler`、`EpollScheduler`、`KqueueScheduler`、`IOUringScheduler`、`WaitResult`、`SpawnAwaitable` 等旧接口从源码树中移除
- `examples/`、`test/`、`benchmark/` 全部迁移到新 runtime/task 模型
- 主干文档只展示新接口路径
- `v3.0.1` 最终指向这次全量切换后的验证通过版本

## 非目标

- 不保留任何旧接口兼容层
- 不在本轮保留“双轨示例”或“底层调度器示例”
- 不为了兼容旧示例保留旧头文件别名

## 方案比较

### 方案 A：一次性硬切

直接删除旧接口并同步迁移所有使用点。

优点：

- 最终结果最干净

缺点：

- 红灯期长
- 很难定位是哪一层断裂导致全仓库失败

### 方案 B：对外删旧、内部保留旧骨架

示例与文档切新，但内部仍保留旧 scheduler/coroutine 骨架。

优点：

- 迁移快

缺点：

- 与“旧接口源码也删除，不作兼容”目标冲突

### 方案 C：分层重构后一次性收口合并

在同一工作分支内按“任务核心 -> runtime 内核 -> awaitable/IO -> examples/test/benchmark/docs -> 物理删除旧接口”顺序推进；合并结果不保留兼容。

优点：

- 最终状态满足零兼容
- 过程可验证、可定位、可回归

缺点：

- 实施步骤多

结论：采用方案 C。

## 最终对外边界

合并后对外公开面统一为：

- `Runtime`
- `RuntimeBuilder`
- `RuntimeHandle`
- `Task<T>`
- `JoinHandle<T>`
- `sleep(...)`
- `TcpSocket`
- `UdpSocket`
- `AsyncFile`
- `AioFile`
- `FileWatcher`
- `AsyncMutex`
- `MpscChannel<T>`
- `UnsafeChannel<T>`
- `AsyncWaiter<T>`

明确移除的公开类型与接口：

- `Coroutine`
- `Scheduler`
- `IOScheduler`
- `ComputeScheduler`
- `EpollScheduler`
- `KqueueScheduler`
- `IOUringScheduler`
- `WaitResult`
- `SpawnAwaitable`
- `Runtime::addIOScheduler(...)`
- `Runtime::addComputeScheduler(...)`
- `Runtime::getIOScheduler(...)`
- `Runtime::getComputeScheduler(...)`
- `Runtime::getNextIOScheduler()`
- `Runtime::getNextComputeScheduler()`

## 新内部结构

### 1. 任务核心层

将当前 `Coroutine.h` 中的任务相关能力独立为新的任务核心模块，仅保留：

- `Task<T>`
- `JoinHandle<T>`
- `TaskRef`
- `TaskState`
- 当前 runtime TLS 绑定逻辑

`Coroutine` 这个名称和包装层被移除。

### 2. Runtime 内核层

`Runtime` 不再直接持有旧 scheduler 类，而是持有内部 worker：

- `IoWorker`
- `ComputeWorker`
- `BlockingExecutor`
- `TimerScheduler`

`Runtime` 负责：

- 生命周期
- 默认 worker 数量配置
- `blockOn(...)`
- `spawn(...)`
- `spawnBlocking(...)`
- `RuntimeHandle`

### 3. Worker 与后端层

内部继续保留已重构出的核心组件：

- `SchedulerCore`
- `WakeCoordinator`
- `BackendReactor`
- `KqueueReactor`
- `EpollReactor`
- `IOUringReactor`

但它们不再通过 `*Scheduler` 公开暴露。

### 4. Awaitable / IO 注册层

`Awaitable` suspend 时不再依赖 `IOScheduler*` 公共类型，而是通过“当前 task 绑定的 runtime/worker 上下文”注册 IO。

恢复路径统一为：

`Task -> 当前 runtime context -> IoWorker -> BackendReactor completion -> WakeCoordinator -> Task resume`

## 物理删改边界

### 删除的旧接口文件

- `galay-kernel/kernel/Coroutine.h`
- `galay-kernel/kernel/Coroutine.cc`
- `galay-kernel/kernel/Scheduler.hpp`
- `galay-kernel/kernel/Scheduler.cc`
- `galay-kernel/kernel/IOScheduler.hpp`
- `galay-kernel/kernel/ComputeScheduler.h`
- `galay-kernel/kernel/ComputeScheduler.cc`
- `galay-kernel/kernel/EpollScheduler.h`
- `galay-kernel/kernel/EpollScheduler.cc`
- `galay-kernel/kernel/KqueueScheduler.h`
- `galay-kernel/kernel/KqueueScheduler.cc`
- `galay-kernel/kernel/IOUringScheduler.h`
- `galay-kernel/kernel/IOUringScheduler.cc`

### 保留但内部化的底层文件

- `galay-kernel/kernel/BackendReactor.h`
- `galay-kernel/kernel/EpollReactor.*`
- `galay-kernel/kernel/KqueueReactor.*`
- `galay-kernel/kernel/IOUringReactor.*`
- `galay-kernel/kernel/SchedulerCore.h`
- `galay-kernel/kernel/WakeCoordinator.h`
- `galay-kernel/kernel/WaitRegistration.h`
- `galay-kernel/kernel/Waker.*`
- `galay-kernel/kernel/TimerScheduler.*`

### 必须同步修改的公开头

- `galay-kernel/kernel/Runtime.h`
- `galay-kernel/kernel/Runtime.cc`
- `galay-kernel/kernel/Awaitable.h`
- `galay-kernel/kernel/Awaitable.inl`
- `galay-kernel/async/TcpSocket.h`
- `galay-kernel/async/TcpSocket.cc`
- `galay-kernel/async/UdpSocket.h`
- `galay-kernel/async/UdpSocket.cc`
- `galay-kernel/async/AsyncFile.*`
- `galay-kernel/async/AioFile.*`
- `galay-kernel/async/FileWatcher.*`

## 迁移顺序

### 阶段 1：任务核心解耦

- 从旧 `Coroutine` 文件中拆出 `Task`/`JoinHandle`/`TaskRef` 真正需要保留的部分
- 让 `Runtime` 不再通过 `Coroutine` 名称或 `Scheduler` 基类组织任务

### 阶段 2：Runtime 内核切换

- `Runtime` 改为只管理内部 worker
- 删除对外 scheduler 管理接口
- 固化新的 root task 提交与 worker 选择路径

### 阶段 3：Awaitable / IO 路径切换

- awaitable suspend / resume 改用 runtime 当前 worker 上下文
- `TcpSocket` / `UdpSocket` / `AsyncFile` / `FileWatcher` 去除对旧 scheduler 头的直接依赖

### 阶段 4：仓库使用点迁移

- `examples/` 全量迁移
- `test/` 全量迁移
- `benchmark/` 全量迁移
- 文档全量迁移

### 阶段 5：物理删除旧接口

- 删除旧头和旧源文件
- 更新 CMake、安装导出、包消费边界
- 跑全量验证

## 错误语义

- 异步 IO 继续统一返回 `std::expected<..., IOError>`
- `runtime.blockOn(...)` / `JoinHandle<T>::join()` 继续传播任务异常
- `RuntimeHandle::current()` 在 runtime 外抛异常，`tryCurrent()` 返回空
- 与旧 scheduler 上下文直接相关的错误概念不再对外暴露

## 验证矩阵

必须 fresh 执行：

1. 新 runtime/task API 核心测试
2. `examples/` 全量构建与运行
3. `test/` 全量通过
4. `benchmark/` 全量 matrix / triplet
5. 三后端完整对比：`kqueue`、`epoll`、`io_uring`

## 必须补的文档

本轮完成后，主干文档新增或重写为：

- 迁移指南：旧接口删除后的替换方式
- 平台 / 后端能力矩阵
- 术语 / 关键词索引

## 发布要求

只有在以下条件全部满足后，才允许重新提交 `v3.0.1`：

- 全量测试通过
- 全量 benchmark 跑完并有最终矩阵
- 主干文档与迁移文档完成
- 删除旧接口后无残留公开引用
- `gh` tag 与 release 指向新的最终 commit，并重新更新 release 说明
