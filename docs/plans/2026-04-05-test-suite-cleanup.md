# Test Suite Cleanup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 删除低价值 benchmark/build 测试，统一 `test/` 编号，并让文档与 target 引用全部对齐。

**Architecture:** 先冻结删除名单和重编号映射，再批量重命名文件并同步替换代码内 `Txx` 标识，最后统一改写文档引用并做定向构建验证。实现只触达 `test/` 与文档层，不改变 CMake 注册方式和运行时逻辑。

**Tech Stack:** CMake, CTest, shell rename/search, markdown docs

---

### Task 1: Freeze Cleanup Scope

**Files:**
- Modify: `docs/plans/2026-04-05-test-suite-cleanup-design.md`
- Test: `test/T51-cmake_source_case.cc`

**Step 1: Confirm deletion set**

删除 14 个 benchmark/build 校验测试，保留功能、回归、后端特性测试。

**Step 2: Confirm renumbering rule**

按现有数字顺序重编号，重复号按文件名字典序稳定排序。

**Step 3: Record the policy**

在设计文档中记录删除策略、重编号策略、文档同步策略。

### Task 2: Delete Low-Value Tests

**Files:**
- Delete: `test/T51-cmake_source_case.cc`
- Delete: `test/T59-benchmark_sync_wait_ready.cc`
- Delete: `test/T60-benchmark_completion_latch.cc`
- Delete: `test/T65-benchmark_start_gate.cc`
- Delete: `test/T66-benchmark_median_element.cc`
- Delete: `test/T67-benchmark_default_scheduler_count.cc`
- Delete: `test/T68-b8_cross_scheduler_source_case.cc`
- Delete: `test/T69-b8_producer_throughput_source_case.cc`
- Delete: `test/T70-b1_throughput_sample_source_case.cc`
- Delete: `test/T71-b8_batch_sample_duration_source_case.cc`
- Delete: `test/T72-b8_single_producer_gate_source_case.cc`
- Delete: `test/T73-b8_single_sample_duration_source_case.cc`
- Delete: `test/T74-b9_throughput_precision_source_case.cc`
- Delete: `test/T75-b9_throughput_sampling_source_case.cc`

**Step 1: Remove the files**

直接删除 14 个确认无保留价值的测试文件。

**Step 2: Verify no doc references remain**

Run: `rg -n "T51-|T59-|T60-|T65-|T66-|T67-|T68-|T69-|T70-|T71-|T72-|T73-|T74-|T75-" docs test`
Expected: no matches

### Task 3: Renumber Remaining Tests

**Files:**
- Modify: `test/*.cc`

**Step 1: Generate deterministic mapping**

生成从旧文件名到新文件名的 `111` 项映射。

**Step 2: Rename files**

按映射批量重命名剩余测试文件。

**Step 3: Rewrite in-file identifiers**

同步更新 `@file`、`PASS/SKIP` 文本和其他显式 `Txx` 标识。

**Step 4: Verify numbering continuity**

Run: `python3 - <<'PY'`
Expected: only `T1..T111` and no duplicates/missing

### Task 4: Align Documentation

**Files:**
- Modify: `docs/*.md`
- Modify: `docs/plans/*.md`

**Step 1: Replace kept test references**

把文档中的旧文件名和 target 名同步改成新编号。

**Step 2: Remove deleted test references**

删掉 14 个已移除测试的文档残留。

**Step 3: Verify no stale references**

Run: `rg -n "test/T(51|59|60|65|66|67|68|69|70|71|72|73|74|75)-|\\bT(51|59|60|65|66|67|68|69|70|71|72|73|74|75)-" docs`
Expected: no matches

### Task 5: Reconfigure and Verify

**Files:**
- Test: `test/CMakeLists.txt`

**Step 1: Reconfigure local kqueue build**

Run: `cmake -S . -B build-codex-kqueue`
Expected: configure succeeds

**Step 2: Build targeted tests**

Run: `cmake --build build-codex-kqueue --target T1-task_chain T39-scheduler_wakeup_coalescing T42-scheduler_queue_edge_wakeup T55-scheduler_reactor_source_case T93-kqueue_sequence_persistent_registration_source_case T105-runtime_fastpath_source_case T111-io_uring_multishot_recv_runtime --parallel 2`
Expected: build succeeds

**Step 3: Run targeted ctest**

Run: `ctest --test-dir build-codex-kqueue --output-on-failure -R "T1-task_chain|T39-scheduler_wakeup_coalescing|T42-scheduler_queue_edge_wakeup|T55-scheduler_reactor_source_case|T93-kqueue_sequence_persistent_registration_source_case|T105-runtime_fastpath_source_case|T111-io_uring_multishot_recv_runtime"`
Expected: relevant tests PASS; io_uring runtime test may SKIP on kqueue

**Step 4: Search for stale or duplicate numbering**

Run: `python3 - <<'PY'`
Expected: exactly `111` tests, no duplicates, no gaps
