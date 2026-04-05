# IOUring Multishot Recv With Provided Buffer Ring Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 `io_uring` 的 plain `recv` 路径引入 `multishot recv + provided buffer ring`，减少重复提交 SQE 和用户态 buffer 直传开销。

**Architecture:** 在 `IOUringReactor` 内维护一组全局 provided buffer ring 缓冲块，`recv` 改为提交持久 `multishot recv` 并通过 `IOSQE_BUFFER_SELECT` 从 ring 取 buffer。完成事件先落到 reactor/controller 管控的内部 ready queue，再在 awaitable 恢复时拷贝到用户传入 buffer，并把内部 buffer 归还到 ring；终态 CQE 再自动重挂 multishot recv。

**Tech Stack:** C++20, liburing provided buffer ring API, existing `IOController` token/state model, CTest runtime/source-case tests

---

### Task 1: 写源码锁定测试

**Files:**
- Create: `test/T110-io_uring_multishot_recv_source_case.cc`

**Step 1: Write the failing test**

- 断言 `IOUringReactor` 使用：
  - `io_uring_setup_buf_ring(...)`
  - `IOSQE_BUFFER_SELECT`
  - `io_uring_prep_recv_multishot(...)`
  - `IORING_CQE_F_BUFFER`
- 断言 `IOController` 增加 recv ready queue / multishot recv 状态

**Step 2: Run test to verify it fails**

Run: `cmake --build build-codex-kqueue --target T110-io_uring_multishot_recv_source_case --parallel 2 && ctest --test-dir build-codex-kqueue -R T110-io_uring_multishot_recv_source_case --output-on-failure`

Expected: FAIL，因为源码锚点尚不存在。

### Task 2: 写最小运行时回归测试

**Files:**
- Create: `test/T111-io_uring_multishot_recv_runtime.cc`

**Step 1: Write the failing test**

- 用 `IOUringScheduler` 跑本地 TCP server/client
- 同一连接连续发送两段 payload
- 服务端连续 `co_await recv(...)` 两次，要求两次都成功拿到正确 payload
- 测试失败条件：
  - 第二次 `recv` 超时
  - 第二次 `recv` 返回错误
  - 两次 payload 任何一次内容不匹配

**Step 2: Run test to verify it fails**

Run on Linux: `cmake --build <iouring-build> --target T111-io_uring_multishot_recv_runtime --parallel 2 && ctest --test-dir <iouring-build> -R T111-io_uring_multishot_recv_runtime --output-on-failure`

Expected: FAIL，因为当前 `recv` 仍是单次 SQE 路径。

### Task 3: 实现 provided buffer ring 和 multishot recv

**Files:**
- Modify: `galay-kernel/kernel/IOController.hpp`
- Modify: `galay-kernel/kernel/IOUringReactor.h`
- Modify: `galay-kernel/kernel/IOUringReactor.cc`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.inl`

**Step 1: 在 reactor 内实现 recv buffer ring 管理**

- 新增内部缓冲池/slot 元数据
- `IOUringReactor` 构造时建立 provided buffer ring
- 析构时注销/释放 ring

**Step 2: 在 controller 内新增 recv multishot 状态和 ready recv queue**

- 跟踪当前 active recv token / armed 状态
- 缓存已经完成但尚未交付给 awaitable 的内部 buffer 片段
- 在 controller 销毁/失效时正确归还内部 buffer

**Step 3: 把 `addRecv()` 改成 multishot recv + buffer select**

- 只对 plain `RecvAwaitable` 生效
- 使用持久 token
- 终态 CQE 自动重挂

**Step 4: 在 completion 路径交付 ready recv 数据**

- 解析 `IORING_CQE_F_BUFFER` 取得 buffer id
- 把 CQE 结果入队到 controller
- 如当前有挂起的 `RecvAwaitable`，立即复制到用户 buffer、设置结果并唤醒
- 数据交付后将 buffer 归还 provided ring

### Task 4: 绿灯验证并做受限 benchmark

**Files:**
- Test: `test/T110-io_uring_multishot_recv_source_case.cc`
- Test: `test/T111-io_uring_multishot_recv_runtime.cc`

**Step 1: Run narrow tests**

- 本地源码测试：`T126`
- 远端 Linux 运行时测试：`T127`

**Step 2: Run relevant regression tests**

- `T121`
- `T123`
- `T124`

**Step 3: Run constrained benchmark**

- 远端 Linux：
  - `taskset -c 1,2 nice -n 10`
  - `B2-TcpServer + B3-TcpClient`
  - `64` connections
  - `8s`
  - `256B / 4096B`

**Step 4: Update todo status**

- 完成后把 `docs/plans/todo.md` 中 `2` 与 `3` 标记为完成
