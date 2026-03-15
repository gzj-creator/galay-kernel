# Awaitable Builder / Sequence Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 以非兼容方式移除旧 `CustomAwaitable/addCustom` 模型，落地新的 `SequenceAwaitable + AwaitableBuilder`，并完成全量迁移、验证与发布。

**Architecture:** 内核以统一 `submitOperation` 驱动 primitive/local 两类 operation，组合 Awaitable 通过 inline queue 维持流程状态，协议解析作为本地步骤执行且未完成时不唤醒 coroutine。对外主入口为 `AwaitableBuilder`，业务库实现 `Flow + parser/handler`，不再继承 Awaitable。

**Tech Stack:** C++20 coroutine、kqueue/epoll/io_uring、CMake、Python unittest、shell benchmark/test runners、GitHub CLI。

---

### Task 1: 建立新的 Sequence 内核骨架

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Modify: `galay-kernel/kernel/KqueueReactor.cc`
- Modify: `galay-kernel/kernel/EpollReactor.cc`
- Modify: `galay-kernel/kernel/IOUringReactor.cc`
- Test: `test/T63-custom_sequence_awaitable.cc`
- Create: `test/T64-awaitable_builder_surface.cc`

**Step 1: Write the failing test**

- Add a new surface test asserting the public API exposes `SequenceAwaitable`, `AwaitableBuilder`, standard primitive builder steps, and no longer exposes the old custom inheritance surface.
- Update `T63` to validate the new sequence path instead of inheritance-based sequence usage.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T63-CustomSequenceAwaitable T64-AwaitableBuilderSurface --parallel`

Expected: build fails because new types/API do not exist yet.

**Step 3: Write minimal implementation**

- Introduce:
  - `OperationRef`
  - `CompletionSink`
  - `SequenceAwaitable<ResultT, InlineN>`
  - `SequenceOps<ResultT>`
  - `SequenceStep<FlowT, BaseContextT, Handler>`
  - `AwaitableBuilder<ResultT, InlineN, FlowT>`
- Keep primitive awaitables building through the same backend helper path.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target T63-CustomSequenceAwaitable T64-AwaitableBuilderSurface --parallel`

Expected: both targets build successfully.

**Step 5: Commit**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc galay-kernel/kernel/IOScheduler.hpp galay-kernel/kernel/KqueueReactor.cc galay-kernel/kernel/EpollReactor.cc galay-kernel/kernel/IOUringReactor.cc test/T63-custom_sequence_awaitable.cc test/T64-awaitable_builder_surface.cc
git commit -m "重构 SequenceAwaitable 内核骨架"
```

### Task 2: 删除 addCustom 并统一后端提交流程

**Files:**
- Modify: `galay-kernel/kernel/IOScheduler.h`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Modify: `galay-kernel/kernel/KqueueReactor.cc`
- Modify: `galay-kernel/kernel/EpollReactor.cc`
- Modify: `galay-kernel/kernel/IOUringReactor.cc`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Test: `test/T30-custom_awaitable.cc`
- Test: `test/T40-io_uring_custom_awaitable_no_null_probe.cc`

**Step 1: Write the failing test**

- Replace old custom-awaitable tests with new operation submission regression tests:
  - sequence local step rearm
  - sequence primitive step submission on three backends
  - io_uring no-null-probe regression still covered via new sequence path

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T30-CustomAwaitable T40-IOUringCustomAwaitableNoNullProbe --parallel`

Expected: build or behavior fails because old tests reference removed API or new operation plumbing is incomplete.

**Step 3: Write minimal implementation**

- Remove `IOScheduler::addCustom`
- Remove `CUSTOM`-specific backend dispatch branches
- Replace with unified `submitOperation` / completion sink flow
- Internalize or delete old custom types

**Step 4: Run test to verify it passes**

Run:
- `cmake --build build --target T30-CustomAwaitable T40-IOUringCustomAwaitableNoNullProbe --parallel`
- `./build/bin/T30-CustomAwaitable`
- `./build/bin/T40-IOUringCustomAwaitableNoNullProbe`

Expected: tests pass, or `T40` prints `SKIP` on unsupported backend.

**Step 5: Commit**

```bash
git add galay-kernel/kernel/IOScheduler.h galay-kernel/kernel/IOScheduler.hpp galay-kernel/kernel/KqueueReactor.cc galay-kernel/kernel/EpollReactor.cc galay-kernel/kernel/IOUringReactor.cc galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc test/T30-custom_awaitable.cc test/T40-io_uring_custom_awaitable_no_null_probe.cc
git commit -m "移除 addCustom 并统一后端提交流程"
```

### Task 3: 落地协议解析与不提前唤醒语义

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Create: `galay-kernel/common/ByteQueueView.h`
- Create: `test/T65-sequence_parser_need_more.cc`
- Create: `test/T66-sequence_parser_coalesced_frames.cc`

**Step 1: Write the failing test**

- Add tests covering:
  - header 不全时不唤醒 coroutine
  - body 不全时不唤醒 coroutine
  - 单次 `recv` 带来多帧粘包时 parse loop 能在一次恢复里尽量吃完

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T65-SequenceParserNeedMore T66-SequenceParserCoalescedFrames --parallel`

Expected: build or runtime fails because parse/local step and buffer view are not complete.

**Step 3: Write minimal implementation**

- Introduce `ByteQueueView`
- Introduce `ParseStatus/ParseResult`
- Make local parse steps run inside sequence driver without waking coroutine until logical completion

**Step 4: Run test to verify it passes**

Run:
- `cmake --build build --target T65-SequenceParserNeedMore T66-SequenceParserCoalescedFrames --parallel`
- `./build/bin/T65-SequenceParserNeedMore`
- `./build/bin/T66-SequenceParserCoalescedFrames`

Expected: both tests pass.

**Step 5: Commit**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc galay-kernel/common/ByteQueueView.h test/T65-sequence_parser_need_more.cc test/T66-sequence_parser_coalesced_frames.cc
git commit -m "补齐协议解析与不提前唤醒语义"
```

### Task 4: 迁移 examples 与 benchmark 到 Builder/Sequence

**Files:**
- Modify: `examples/**`
- Modify: `benchmark/**`
- Modify: `examples/CMakeLists.txt`
- Modify: `benchmark/CMakeLists.txt`
- Test: affected example smoke runs
- Test: affected benchmark compile/run

**Step 1: Write the failing test**

- Update example and benchmark sources to only use the new builder/sequence API.
- Remove any remaining include or source references to old custom awaitable interfaces.

**Step 2: Run test to verify it fails**

Run:
- `rg -n "CustomAwaitable|CustomSequenceAwaitable|addCustom" examples benchmark`
- `cmake --build build --target examples benchmarks --parallel`

Expected: references/build failures remain until migration is complete.

**Step 3: Write minimal implementation**

- Port all examples/benchmarks to builder-based API or equivalent new sequence API
- Remove obsolete sources and helper code

**Step 4: Run test to verify it passes**

Run:
- `rg -n "CustomAwaitable|CustomSequenceAwaitable|addCustom" examples benchmark galay-kernel || true`
- `cmake --build build --parallel`

Expected: no old references in public code, build succeeds.

**Step 5: Commit**

```bash
git add examples benchmark
git commit -m "迁移示例与压测到新 Awaitable 接口"
```

### Task 5: 迁移正式文档与升级说明

**Files:**
- Modify: `README.md`
- Modify: `docs/00-快速开始.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/04-示例代码.md`
- Modify: `docs/06-高级主题.md`
- Modify: other docs referencing removed custom APIs

**Step 1: Write the failing test**

- Add or update doc expectations by removing old API names and documenting builder/sequence/protocol parse semantics.

**Step 2: Run test to verify it fails**

Run:
- `rg -n "CustomAwaitable|CustomSequenceAwaitable|addCustom" README.md docs`

Expected: old API references still exist.

**Step 3: Write minimal implementation**

- Update docs for:
  - new builder/sequence APIs
  - protocol parse / half-packet / sticky-packet semantics
  - `v3.1.0` breaking-change guidance

**Step 4: Run test to verify it passes**

Run:
- `rg -n "CustomAwaitable|CustomSequenceAwaitable|addCustom" README.md docs || true`
- `git diff --check -- README.md docs`

Expected: no stale public doc references, diff check clean.

**Step 5: Commit**

```bash
git add README.md docs
git commit -m "更新 v3.1.0 Awaitable 正式文档"
```

### Task 6: 全量验证、发布与远端同步

**Files:**
- Modify: release notes temp file if needed
- Verify: `test/`, `examples/`, `benchmark/`

**Step 1: Run full verification**

Run:
- `python3 -m unittest scripts.tests.test_run_test_matrix scripts.tests.test_run_benchmark_matrix scripts.tests.test_run_benchmark_triplet scripts.tests.test_run_single_benchmark_triplet scripts.tests.test_parse_benchmark_triplet`
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON -DBUILD_EXAMPLES=ON`
- `cmake --build build --parallel`
- `bash scripts/run_test_matrix.sh "$PWD/build" "$PWD/build/test_matrix_logs_2026_03_15_v310"`
- `bash scripts/run_benchmark_matrix.sh --build-dir "$PWD/build" --log-root "$PWD/build/benchmark_matrix_logs_2026_03_15_v310"`

Expected: 全量 test/example/benchmark 成功，无新的 crash/timeout/fail 标记。

**Step 2: Commit final verification-sensitive changes**

```bash
git add -A
git commit -m "完成 v3.1.0 Awaitable Builder 非兼容迁移"
```

**Step 3: Tag and release**

Run:

```bash
git tag -f v3.1.0
gh release create v3.1.0 --title "v3.1.0" --notes-file /tmp/galay-kernel-v3.1.0-release-notes.md
```

If the normal `git push` transport fails, use GitHub API fallback to sync `main`, `v3.1.0`, and release target.

**Step 4: Verify remote state**

Run:

```bash
gh api repos/gzj-creator/galay-kernel/git/ref/heads/main --jq .object.sha
gh api repos/gzj-creator/galay-kernel/git/ref/tags/v3.1.0 --jq .object.sha
gh release view v3.1.0 --json tagName,targetCommitish,url
```

Expected: remote `main` / `v3.1.0` / release target are aligned.
