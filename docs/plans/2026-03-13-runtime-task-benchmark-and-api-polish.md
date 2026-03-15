# Runtime Task Benchmark And API Polish Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 runtime/task 重构补齐三组 benchmark 证据链，完成高层 API 收口，并用 benchmark 与回归测试证明性能与兼容性。

**Architecture:** 先把 benchmark 运行与结果汇总做成可复用的脚本化流程，覆盖 `pre-v3`、`v3`、`refactored` 三组基线；再用一条编译期 API surface 测试驱动 runtime/task 公开接口收口，最后回到三组 benchmark 和关键回归测试做收口验证。性能优化只在 benchmark 结果明确暴露回退时进行，不凭感觉提前改实现。

**Tech Stack:** CMake、bash、Python 3 `unittest`、现有 benchmark targets、C++23、现有 runtime/task 测试集。

---

### Task 1: 为 benchmark matrix runner 增加可测试的重复采样能力

**Files:**
- Modify: `scripts/run_benchmark_matrix.sh`
- Create: `scripts/tests/test_run_benchmark_matrix.py`

**Step 1: 写 runner 的 failing smoke test**

在 `scripts/tests/test_run_benchmark_matrix.py` 中写一个 `unittest`：

- 在临时目录创建 fake `build/bin`
- 生成最小假 benchmark 可执行文件：
  - standalone: `B1-ComputeScheduler`、`B6-Udp`、`B7-FileIo`、`B8-MpscChannel`、`B9-UnsafeChannel`、`B10-Ringbuffer`、`B13-Sendfile`、`B14-SchedulerInjectedWakeup`
  - paired: `B2-TcpServer`、`B3-TcpClient`、`B4-UdpServer`、`B5-UdpClient`、`B11-TcpIovServer`、`B12-TcpIovClient`
- 让脚本支持新参数：
  - `--repeat 2`
  - `--label refactored`
  - `--log-root <tmpdir>`
- 断言脚本会生成按采样编号区分的日志目录，例如：
  - `<log-root>/refactored/run-1/B1-ComputeScheduler.log`
  - `<log-root>/refactored/run-2/B2-TcpServer.log`

**Step 2: 运行 test，确认正确失败**

Run: `python3 -m unittest scripts.tests.test_run_benchmark_matrix -v`

Expected: FAIL，原因是 `scripts/run_benchmark_matrix.sh` 还不支持 `--repeat` / `--label` / 分 run 输出目录。

**Step 3: 实现最小 runner 扩展**

修改 `scripts/run_benchmark_matrix.sh`：

- 支持命名参数：
  - `--build-dir`
  - `--log-root`
  - `--repeat`
  - `--label`
- 保持现有默认行为兼容
- 每次采样输出到独立目录：
  - `<log-root>/<label>/run-<n>/...`
- 保持 paired benchmark 的 server/client 启停与端口逻辑不变

**Step 4: 运行 test，确认通过**

Run: `python3 -m unittest scripts.tests.test_run_benchmark_matrix -v`

Expected: PASS。

**Step 5: Commit**

```bash
git add scripts/run_benchmark_matrix.sh scripts/tests/test_run_benchmark_matrix.py
git commit -m "test: cover benchmark matrix runner repeats"
```

说明：仅在用户明确要求提交时执行。

### Task 2: 为 benchmark 结果解析与三组对比添加可测试的汇总器

**Files:**
- Create: `scripts/parse_benchmark_triplet.py`
- Create: `scripts/tests/test_parse_benchmark_triplet.py`

**Step 1: 写 parser 的 failing tests**

在 `scripts/tests/test_parse_benchmark_triplet.py` 中写 `unittest`，覆盖：

- 从代表性日志片段提取主指标：
  - `B1-ComputeScheduler` 的 throughput / latency
  - `B8-MpscChannel` 的 throughput / latency
  - `B14-SchedulerInjectedWakeup` 的 injected throughput / avg latency
- 计算多次采样的 median
- 输出三组对比表格所需的数据结构：
  - benchmark 名称
  - metric 名称
  - `pre-v3` / `v3` / `refactored`
  - 相对 `v3` delta
  - pass/fail 状态

**Step 2: 运行 test，确认正确失败**

Run: `python3 -m unittest scripts.tests.test_parse_benchmark_triplet -v`

Expected: FAIL，原因是解析脚本还不存在。

**Step 3: 实现最小 parser**

在 `scripts/parse_benchmark_triplet.py` 中实现：

- 读取三组目录下每个 benchmark 的多次日志
- 按 benchmark 类型匹配主指标
- 计算 median
- 生成：
  - 机器可读 JSON
  - markdown summary table

**Step 4: 运行 test，确认通过**

Run: `python3 -m unittest scripts.tests.test_parse_benchmark_triplet -v`

Expected: PASS。

**Step 5: Commit**

```bash
git add scripts/parse_benchmark_triplet.py scripts/tests/test_parse_benchmark_triplet.py
git commit -m "feat: add benchmark triplet result parser"
```

说明：仅在用户明确要求提交时执行。

### Task 3: 增加三组 benchmark orchestration 脚本

**Files:**
- Create: `scripts/run_benchmark_triplet.sh`
- Create: `scripts/tests/test_run_benchmark_triplet.py`
- Modify: `scripts/run_benchmark_matrix.sh`

**Step 1: 写 triplet runner 的 failing dry-run test**

在 `scripts/tests/test_run_benchmark_triplet.py` 中写 `unittest`：

- 让脚本支持 `--dry-run`
- 输入固定三组基线：
  - `--pre-v3-ref 09c8917`
  - `--v3-ref cde3da1`
  - `--refactored-path .worktrees/v3`
- 断言 dry-run 输出会包含：
  - worktree 路径
  - configure/build 命令
  - `scripts/run_benchmark_matrix.sh --repeat 3 ...`
  - 对 `B14` 的 compat build / compat run 步骤

**Step 2: 运行 test，确认正确失败**

Run: `python3 -m unittest scripts.tests.test_run_benchmark_triplet -v`

Expected: FAIL，原因是 triplet runner 还不存在。

**Step 3: 实现最小 triplet runner**

在 `scripts/run_benchmark_triplet.sh` 中实现：

- 创建 / 复用以下 worktree：
  - `.worktrees/pre-v3-bench`
  - `.worktrees/v3-baseline`
  - `.worktrees/v3`
- 为三组源码统一执行 configure + build
- 调用 `scripts/run_benchmark_matrix.sh` 采样
- 对历史基线缺失的 `B14` 走 compat build / compat run
- 支持 `--dry-run`
- 支持 `--repeat`
- 支持自定义输出目录

**Step 4: 运行 test，确认通过**

Run: `python3 -m unittest scripts.tests.test_run_benchmark_triplet -v`

Expected: PASS。

**Step 5: Commit**

```bash
git add scripts/run_benchmark_triplet.sh scripts/tests/test_run_benchmark_triplet.py scripts/run_benchmark_matrix.sh
git commit -m "feat: add benchmark triplet runner"
```

说明：仅在用户明确要求提交时执行。

### Task 4: 跑第一版三组 benchmark，并生成结果文档

**Files:**
- Create: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/05-性能测试.md`

**Step 1: 运行三组 benchmark matrix**

Run:

```bash
./scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --repeat 3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet
```

Expected: 三组 benchmark 日志全部产出；若某个历史基线不支持兼容 benchmark，脚本明确标记 `unsupported` 而不是静默跳过。

**Step 2: 解析结果并生成 markdown 汇总**

Run:

```bash
python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md
```

Expected: 结果文档包含三组表格、相对 `v3` 的 delta 以及 pass/fail 标记。

**Step 3: 更新性能文档入口**

在 `docs/05-性能测试.md` 中加入：

- 三组 benchmark 对比文档链接
- 当前 benchmark matrix 脚本入口

**Step 4: Commit**

```bash
git add docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md docs/05-性能测试.md
git commit -m "docs: record runtime task benchmark triplet results"
```

说明：仅在用户明确要求提交时执行。

### Task 5: 用编译期 surface test 驱动 runtime/task API 收口

**Files:**
- Create: `test/T57-runtime_task_api_surface.cc`
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `galay-kernel/kernel/Coroutine.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `examples/include/E4-coroutine_basic.cc`
- Modify: `examples/import/E4-coroutine_basic.cc`
- Modify: `README.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/18-运行时Runtime.md`

**Step 1: 写 failing API surface test**

在 `test/T57-runtime_task_api_surface.cc` 中写编译期 / 运行期混合测试：

- 用 `requires` / `concept` 断言以下接口**不存在**：
  - `JoinHandle<int>::result()`
  - `Task<int>::taskRef()`
  - `Task<int>::belongScheduler()`
  - `Task<int>::belongScheduler(Scheduler*)`
  - `Task<int>::threadId()`
  - `Task<int>::asCoroutine()`
- 同时验证以下高层 API 仍然可用：
  - `Runtime::blockOn(...)`
  - `Runtime::spawn(...)`
  - `Runtime::handle()`
  - `RuntimeHandle::spawn(...)`
  - `RuntimeHandle::spawnBlocking(...)`

**Step 2: 运行 test，确认正确失败**

Run: `cmake --build build --target T57-runtime_task_api_surface -j4`

Expected: FAIL，原因是当前公开 surface 仍然暴露了重复接口。

**Step 3: 实现最小 API 收口**

修改 runtime/task 头与实现：

- 删除 `JoinHandle::result()`
- 将 `Task<T>` 的低层 helper 收回 private / internal detail
- 保持 `Runtime` / `RuntimeHandle` / `JoinHandle::join()` / `JoinHandle::wait()` 路径不变
- 若内部仍需访问低层 helper，改成 friend / detail bridge，而不是继续公开

**Step 4: 更新示例与文档主路径**

统一只展示高层路径：

- `Runtime::blockOn(...)`
- `Runtime::spawn(...)`
- `RuntimeHandle`

不再把低层 helper 当成公开推荐 API。

**Step 5: 运行 test，确认通过**

Run:

```bash
cmake --build build --target T57-runtime_task_api_surface E4-CoroutineBasic -j4
./build/bin/T57-runtime_task_api_surface
./build/bin/E4-CoroutineBasic
```

Expected: PASS。

**Step 6: Commit**

```bash
git add test/T57-runtime_task_api_surface.cc galay-kernel/kernel/Coroutine.h galay-kernel/kernel/Coroutine.cc galay-kernel/kernel/Runtime.h galay-kernel/kernel/Runtime.cc examples/include/E4-coroutine_basic.cc examples/import/E4-coroutine_basic.cc README.md docs/03-使用指南.md docs/18-运行时Runtime.md
git commit -m "refactor: tighten runtime task api surface"
```

说明：仅在用户明确要求提交时执行。

### Task 6: 做最终 benchmark / regression 收口验证

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-design.md`

**Step 1: 重新跑三组 benchmark**

Run:

```bash
./scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --repeat 3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-final

python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-final \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md
```

Expected:

- 核心 benchmark 相对 `v3` 更优
- 其余 benchmark 无超过 `10%` 的明显回退

如果不满足预期：停止执行，带着结果回到设计层，而不是猜测性继续优化。

**Step 2: 运行关键回归测试**

Run:

```bash
cmake --build build --target \
  T1-coroutine_chain \
  T25-spawn_simple \
  T27-runtime_stress \
  T36-task_core_wake_path \
  T39-awaitable_hot_path_helpers \
  T42-runtime_strict_scheduler_counts \
  T43-wait_continuation_scheduler_path \
  T44-scheduler_wakeup_coalescing \
  T45-scheduler_ready_budget \
  T46-scheduler_injected_burst_fastpath \
  T47-scheduler_queue_edge_wakeup \
  T48-scheduler_core_loop_stages \
  T49-channel_wait_registration \
  T50-compute_scheduler_taskref_fastpath \
  T52-runtime_block_on_result \
  T53-runtime_block_on_exception \
  T54-runtime_spawn_join_handle \
  T55-runtime_handle_current \
  T56-runtime_spawn_blocking \
  T57-runtime_task_api_surface \
  -j4

./build/bin/T1-coroutine_chain
./build/bin/T25-spawn_simple
./build/bin/T27-runtime_stress
./build/bin/T36-task_core_wake_path
./build/bin/T39-awaitable_hot_path_helpers
./build/bin/T42-runtime_strict_scheduler_counts
./build/bin/T43-wait_continuation_scheduler_path
./build/bin/T44-scheduler_wakeup_coalescing
./build/bin/T45-scheduler_ready_budget
./build/bin/T46-scheduler_injected_burst_fastpath
./build/bin/T47-scheduler_queue_edge_wakeup
./build/bin/T48-scheduler_core_loop_stages
./build/bin/T49-channel_wait_registration
./build/bin/T50-compute_scheduler_taskref_fastpath
./build/bin/T52-runtime_block_on_result
./build/bin/T53-runtime_block_on_exception
./build/bin/T54-runtime_spawn_join_handle
./build/bin/T55-runtime_handle_current
./build/bin/T56-runtime_spawn_blocking
./build/bin/T57-runtime_task_api_surface
```

Expected: PASS。

**Step 3: 记录结果与结论**

在 `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md` 和 design doc 中补充：

- 最终 benchmark 结论
- API 收口结论
- 是否达到验收标准

**Step 4: Commit**

```bash
git add docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-design.md
git commit -m "docs: finalize runtime task benchmark and api polish results"
```

说明：仅在用户明确要求提交时执行。
