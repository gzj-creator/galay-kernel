# Scheduler Finish And Verify Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 完成 `Scheduler` 分层收尾、补齐 `CustomAwaitable` 易用性改造、修复全量测试矩阵遗漏，并用当前代码重新完成 benchmark/documentation 验证。

**Architecture:** 先用最小 failing tests 锁定剩余缺口，再把 backend-specific poll/register/completion 迁到独立 `*Reactor` 组件，让 scheduler 只保留调度和唤醒协调；随后补一个更易写的自定义组合 IO 骨架，最后用当前 worktree 重新跑测试和 benchmark，刷新文档口径。

**Tech Stack:** C++23、C++20 协程、kqueue/epoll/io_uring、CMake、bash、Python unittest。

---

### Task 1: 锁定剩余缺口

**Files:**
- Create: `scripts/tests/test_run_test_matrix.py`
- Create: `test/T62-scheduler_reactor_source_case.cc`
- Create: `test/T63-custom_sequence_awaitable.cc`

**Step 1: 写 `run_test_matrix.sh` 的 failing test**

- 验证脚本会覆盖 `T57-runtime_task_api_surface`、`T58`、`T59`、`T60`、`T61`
- 优先验证脚本按当前 `build/bin` 可执行文件集合生成完整矩阵

**Step 2: 写 backend reactor 分层的 failing source-case test**

- 验证 `BackendReactor.h`、`KqueueReactor.h`、`EpollReactor.h`、`IOUringReactor.h` 存在
- 验证 scheduler 实现已包含对应 reactor 头文件而不是继续把所有 backend 逻辑堆在一个类里

**Step 3: 写新 custom awaitable authoring API 的 failing test**

- 用比 `T30` 更短的写法实现同样的 SEND -> RECV 组合 IO
- 验证无需手写 `m_tasks` / `front()` / `popFront()` 管理逻辑

### Task 2: 完成内核收尾

**Files:**
- Create: `galay-kernel/kernel/BackendReactor.h`
- Create: `galay-kernel/kernel/KqueueReactor.h`
- Create: `galay-kernel/kernel/KqueueReactor.cc`
- Create: `galay-kernel/kernel/EpollReactor.h`
- Create: `galay-kernel/kernel/EpollReactor.cc`
- Create: `galay-kernel/kernel/IOUringReactor.h`
- Create: `galay-kernel/kernel/IOUringReactor.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.h`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`

**Step 1: 抽出 backend reactor**

- 将 backend-specific fd/ring/notify/register/poll/completion 逻辑迁到各自 reactor
- scheduler 保留 start/stop、remote enqueue、wake arbitration、ready loop

**Step 2: 补新 custom awaitable 骨架**

- 提供顺序组合 IO helper，让用户只描述 step 和完成条件
- 保持现有 `CustomAwaitable` 兼容，不破坏旧代码

### Task 3: 补齐脚本与文档

**Files:**
- Modify: `scripts/run_test_matrix.sh`
- Modify: `scripts/tests/test_run_benchmark_matrix.py`
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`

**Step 1: 让 test matrix 真正覆盖当前测试集**

- 至少纳入 `T57-runtime_task_api_surface`、`T58`、`T59`、`T60`、`T61`
- 保持 server/client pair 运行方式不变

**Step 2: benchmark/docs 统一到当前 code state**

- 删除“旧报告仍代表当前代码”的歧义
- 只保留 fresh rerun 结果作为对外口径

### Task 4: 重新验证

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`

**Step 1: 跑 fresh build + full test matrix**

Run: `bash .worktrees/v3/scripts/run_test_matrix.sh /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/test_matrix_logs_fresh`

Expected: exit code `0`；平台受限项允许 `SKIP`。

**Step 2: 跑 fresh benchmark triplet**

Run: `bash .worktrees/v3/scripts/run_benchmark_triplet.sh --pre-v3-ref 09c8917 --v3-ref cde3da1 --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 --repeat 5 --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-current`

Expected: 解析后 `overall_pass = true`。

**Step 3: 同步文档**

- 用 fresh rerun 的 report 更新对比矩阵
- 明确区分本地 `kqueue` 与 Linux `epoll/io_uring` 的验证边界
