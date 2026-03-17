# Awaitable 状态机 Builder Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a state-machine-backed awaitable core to `galay-kernel`, expose it through `AwaitableBuilder::fromStateMachine(...)`, and keep the existing linear builder surface by compiling it into an internal `LinearMachine`.

**Architecture:** Reuse the existing `SEQUENCE` scheduling path instead of inventing a new reactor protocol. First add the state-machine action model and `StateMachineAwaitable`, then bridge `AwaitableBuilder` into that core, and finally verify that the existing parser/builder regressions still pass unchanged.

**Tech Stack:** C++23, coroutine awaitables, `epoll`, `kqueue`, `io_uring`, CMake, socketpair-based regression tests, Git worktrees

---

### Task 1: Establish isolated worktree and baseline

**Files:**
- Inspect: `galay-kernel/kernel/Awaitable.h`
- Inspect: `galay-kernel/kernel/Awaitable.cc`
- Inspect: `galay-kernel/kernel/IOScheduler.hpp`
- Inspect: `galay-kernel/kernel/EpollReactor.cc`
- Inspect: `galay-kernel/kernel/KqueueReactor.cc`
- Inspect: `galay-kernel/kernel/IOUringReactor.cc`
- Inspect: `test/T30-custom_awaitable.cc`
- Inspect: `test/T63-custom_sequence_awaitable.cc`
- Inspect: `test/T76-sequence_parser_need_more.cc`
- Inspect: `test/T77-sequence_parser_coalesced_frames.cc`

**Step 1: Create a dedicated worktree**

Run:

```bash
git worktree add .worktrees/awaitable-state-machine -b awaitable-state-machine
```

Expected: a clean isolated worktree is created for the feature.

**Step 2: Configure a fresh build directory**

Run:

```bash
cmake -S . -B build-awaitable-state-machine -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
```

Expected: configure succeeds before any code changes.

**Step 3: Record the current builder/sequence surface**

Run:

```bash
rg -n "SequenceAwaitable|SequenceStep|AwaitableBuilder|ParseStatus|ByteQueueView|addSequence|SEQUENCE" galay-kernel test docs README.md
```

Expected: hits in `Awaitable.h`, the three reactors, and the existing sequence tests.

**Step 4: Commit the baseline note if you create one**

```bash
git status --short
```

Expected: clean worktree before adding tests.

### Task 2: Lock the new public surface with failing tests

**Files:**
- Create: `test/T85-state_machine_awaitable_surface.cc`
- Create: `test/T86-state_machine_read_write_loop.cc`
- Create: `test/T87-awaitable_builder_state_machine_bridge.cc`

**Step 1: Write the failing public-surface test**

Create `test/T85-state_machine_awaitable_surface.cc` to require:

- `MachineSignal`
- `MachineAction<ResultT>`
- `AwaitableStateMachine`
- `StateMachineAwaitable<MachineT>`
- `AwaitableBuilder<ResultT>::fromStateMachine(...)`

The test should use `static_assert` and simple dummy machine types, for example:

```cpp
struct DummyMachine {
    using result_type = std::expected<size_t, IOError>;

    MachineAction<result_type> advance() {
        return MachineAction<result_type>::complete(result_type{0});
    }

    void onRead(std::expected<size_t, IOError>) {}
    void onWrite(std::expected<size_t, IOError>) {}
};
```

**Step 2: Write the failing read/write loop regression**

Create `test/T86-state_machine_read_write_loop.cc` using `socketpair(...)` and a custom machine that exercises:

- first `WaitForRead`
- then `WaitForWrite`
- then another `WaitForRead`
- finally `Complete`

The test should fail to compile or link until `StateMachineAwaitable` exists.

**Step 3: Write the failing builder bridge test**

Create `test/T87-awaitable_builder_state_machine_bridge.cc` that constructs a normal builder:

```cpp
auto sequence = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .recv<&Flow::onRecv>(flow.scratch, sizeof(flow.scratch))
    .parse<&Flow::onParse>()
    .send<&Flow::onSend>(payload.data(), payload.size())
    .build();
```

and asserts the resulting type is valid to `co_await`.

**Step 4: Run the new targets and confirm they fail for the right reasons**

Run:

```bash
cmake --build build-awaitable-state-machine --target T85-state_machine_awaitable_surface T86-state_machine_read_write_loop T87-awaitable_builder_state_machine_bridge --parallel
```

Expected: FAIL because the new state-machine types and builder entry point do not exist yet.

**Step 5: Commit the failing tests**

```bash
git add test/T85-state_machine_awaitable_surface.cc test/T86-state_machine_read_write_loop.cc test/T87-awaitable_builder_state_machine_bridge.cc
git commit -m "新增 Awaitable 状态机公开面失败测试"
```

### Task 3: Add the state-machine core types in `Awaitable.h`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Test: `test/T85-state_machine_awaitable_surface.cc`

**Step 1: Add the action model**

In `galay-kernel/kernel/Awaitable.h`, add:

- `enum class MachineSignal`
- `template <typename ResultT> struct MachineAction`
- factory helpers:
  - `continue_()`
  - `waitRead(...)`
  - `waitWrite(...)`
  - `complete(...)`
  - `fail(...)`

Keep `Fail` typed as `IOError`, not as arbitrary domain error. Domain errors should travel inside `result_type`.

**Step 2: Add the machine concept**

Still in `Awaitable.h`, add `AwaitableStateMachine` with:

- `using result_type`
- `advance() -> MachineAction<result_type>`
- `onRead(std::expected<size_t, IOError>)`
- `onWrite(std::expected<size_t, IOError>)`

**Step 3: Add the core awaitable shell**

Add `template <AwaitableStateMachine MachineT> class StateMachineAwaitable`.

For this task, it is enough to declare:

- constructor
- `await_ready()`
- `await_suspend(...)`
- `await_resume()`
- `prepareForSubmit(...)`
- `onActiveEvent(...)`

Make it derive from `SequenceAwaitableBase` so the current `SEQUENCE` reactor path can be reused.

**Step 4: Implement the minimal compile path**

In `Awaitable.cc`, add the minimal helper implementations needed to make `T85-state_machine_awaitable_surface` compile.

Do not wire builder bridging yet.

**Step 5: Run the surface test**

Run:

```bash
cmake --build build-awaitable-state-machine --target T85-state_machine_awaitable_surface --parallel
./build-awaitable-state-machine/bin/T85-state_machine_awaitable_surface
```

Expected: PASS.

**Step 6: Commit the state-machine core surface**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc test/T85-state_machine_awaitable_surface.cc
git commit -m "添加 Awaitable 状态机核心公开类型"
```

### Task 4: Implement `StateMachineAwaitable` on top of `SEQUENCE`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Inspect: `galay-kernel/kernel/EpollReactor.cc`
- Inspect: `galay-kernel/kernel/KqueueReactor.cc`
- Inspect: `galay-kernel/kernel/IOUringReactor.cc`
- Test: `test/T86-state_machine_read_write_loop.cc`

**Step 1: Add internal active-step storage**

Inside `StateMachineAwaitable`, store:

- internal `RecvIOContext`
- internal `SendIOContext`
- one current `IOTask`
- result storage
- a small inline-pump transition counter

Do not allocate per transition.

**Step 2: Implement `pump()`**

`pump()` should:

- repeatedly call `machine.advance()`
- map `Continue` to local loop
- map `WaitForRead` to the internal recv context and active task
- map `WaitForWrite` to the internal send context and active task
- map `Complete` to final result and clear active task
- map `Fail` to final error and clear active task

Add a hard inline transition cap, such as `64`, and finish with `IOError(kParamInvalid, 0)` or a dedicated loop-exceeded error if the machine spins forever.

**Step 3: Implement IO completion feeding**

In `onActiveEvent(...)`, when the active task is read:

- call `RecvIOContext::handleComplete(...)`
- if complete, move `m_result` out of the recv context
- pass it to `machine.onRead(...)`
- re-enter `pump()`

Do the symmetric path for write.

**Step 4: Verify all three backends still work through the old `SEQUENCE` protocol**

Do not edit reactor code unless compilation or runtime proves it necessary. The intended design is to keep:

- `addSequence(...)`
- `resolveTaskEventType(...)`
- `onActiveEvent(...)`

unchanged at the backend boundary.

**Step 5: Run the new loop test**

Run:

```bash
cmake --build build-awaitable-state-machine --target T86-state_machine_read_write_loop --parallel
./build-awaitable-state-machine/bin/T86-state_machine_read_write_loop
```

Expected: PASS; the machine should successfully exercise a `read -> write -> read -> complete` loop.

**Step 6: Commit the runtime driver**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc test/T86-state_machine_read_write_loop.cc
git commit -m "实现基于 SEQUENCE 的状态机 Awaitable 驱动"
```

### Task 5: Add `AwaitableBuilder::fromStateMachine(...)`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T85-state_machine_awaitable_surface.cc`

**Step 1: Add a small builder wrapper**

Add a tiny `StateMachineBuilder<MachineT>` or equivalent helper that stores:

- `IOController*`
- `MachineT`

and whose `build()` returns `StateMachineAwaitable<MachineT>`.

**Step 2: Add the public builder entry**

Add:

```cpp
template <AwaitableStateMachine MachineT>
static auto fromStateMachine(IOController* controller, MachineT machine);
```

to `AwaitableBuilder`.

The return type can be the tiny wrapper from step 1.

**Step 3: Keep the old chain API source-compatible**

Do not break:

- `.recv(...)`
- `.parse(...)`
- `.send(...)`
- `.build()`

at this stage.

**Step 4: Re-run the surface test**

Run:

```bash
cmake --build build-awaitable-state-machine --target T85-state_machine_awaitable_surface --parallel
./build-awaitable-state-machine/bin/T85-state_machine_awaitable_surface
```

Expected: PASS with the new static entry point available.

**Step 5: Commit the builder entry**

```bash
git add galay-kernel/kernel/Awaitable.h test/T85-state_machine_awaitable_surface.cc
git commit -m "为 AwaitableBuilder 添加状态机入口"
```

### Task 6: Compile the linear builder into an internal `LinearMachine`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Test: `test/T63-custom_sequence_awaitable.cc`
- Test: `test/T76-sequence_parser_need_more.cc`
- Test: `test/T77-sequence_parser_coalesced_frames.cc`
- Test: `test/T87-awaitable_builder_state_machine_bridge.cc`

**Step 1: Add `detail::LinearMachine<ResultT, InlineN, FlowT>`**

The internal machine should model the existing builder nodes:

- recv node
- parse node
- send node
- finish node

It should keep:

- current node index
- last recv node index for `kNeedMore`
- `FlowT*`
- builder-owned node storage

**Step 2: Preserve parser semantics**

Map `ParseStatus` exactly as follows:

- `kNeedMore` -> arm the last recv node and return `WaitForRead`
- `kContinue` -> return `Continue`
- `kCompleted` -> move to the next node

Do not regress the existing half-packet or coalesced-frame behavior.

**Step 3: Change chained `build()` to return a machine-backed awaitable**

The public builder should now build:

- `StateMachineAwaitable<detail::LinearMachine<...>>`

instead of owning an independent sequence execution path.

**Step 4: Run the builder bridge and parser regressions**

Run:

```bash
cmake --build build-awaitable-state-machine --target T63-custom_sequence_awaitable T76-sequence_parser_need_more T77-sequence_parser_coalesced_frames T87-awaitable_builder_state_machine_bridge --parallel
./build-awaitable-state-machine/bin/T63-custom_sequence_awaitable
./build-awaitable-state-machine/bin/T76-sequence_parser_need_more
./build-awaitable-state-machine/bin/T77-sequence_parser_coalesced_frames
./build-awaitable-state-machine/bin/T87-awaitable_builder_state_machine_bridge
```

Expected: all PASS with unchanged observable semantics.

**Step 5: Commit the bridge**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc test/T63-custom_sequence_awaitable.cc test/T76-sequence_parser_need_more.cc test/T77-sequence_parser_coalesced_frames.cc test/T87-awaitable_builder_state_machine_bridge.cc
git commit -m "将线性 AwaitableBuilder 编译为内部状态机"
```

### Task 7: Full regression and documentation sync

**Files:**
- Modify: `docs/02-API参考.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/06-高级主题.md`
- Modify: `README.md`

**Step 1: Update the docs**

Document:

- the new state-machine action model
- `AwaitableBuilder::fromStateMachine(...)`
- the fact that the linear builder now targets the shared state-machine core
- the continued availability of `SequenceAwaitable + SequenceStep` for explicit queue-based composition

**Step 2: Run the complete targeted regression set**

Run:

```bash
cmake --build build-awaitable-state-machine --target T30-custom_awaitable T40-io_uring_custom_awaitable_no_null_probe T63-custom_sequence_awaitable T76-sequence_parser_need_more T77-sequence_parser_coalesced_frames T85-state_machine_awaitable_surface T86-state_machine_read_write_loop T87-awaitable_builder_state_machine_bridge --parallel
./build-awaitable-state-machine/bin/T30-custom_awaitable
./build-awaitable-state-machine/bin/T63-custom_sequence_awaitable
./build-awaitable-state-machine/bin/T76-sequence_parser_need_more
./build-awaitable-state-machine/bin/T77-sequence_parser_coalesced_frames
./build-awaitable-state-machine/bin/T85-state_machine_awaitable_surface
./build-awaitable-state-machine/bin/T86-state_machine_read_write_loop
./build-awaitable-state-machine/bin/T87-awaitable_builder_state_machine_bridge
```

Expected: PASS on the current platform backend. If the backend is `io_uring`, also run `T40-io_uring_custom_awaitable_no_null_probe`.

**Step 3: Inspect the final diff**

Run:

```bash
git status --short
git diff --stat
```

Expected: only the expected kernel, test, and docs files are modified.

**Step 4: Commit the finished feature**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc docs/02-API参考.md docs/03-使用指南.md docs/06-高级主题.md README.md test/T85-state_machine_awaitable_surface.cc test/T86-state_machine_read_write_loop.cc test/T87-awaitable_builder_state_machine_bridge.cc
git commit -m "引入 Awaitable 状态机 Builder 双模式模型"
```

### Task 8: Downstream validation in `galay-ssl`

**Files:**
- Inspect: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/galay-ssl/async/Awaitable.h`
- Inspect: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/galay-ssl/async/Awaitable.cc`

**Step 1: Record the intended downstream pilot**

Use `SslRecvAwaitable` as the first real migration candidate after kernel lands.

Run:

```bash
rg -n "SslRecvAwaitable|SslHandshakeAwaitable|SslShutdownAwaitable" /Users/gongzhijie/Desktop/projects/git/galay-ssl/galay-ssl/async/Awaitable.h /Users/gongzhijie/Desktop/projects/git/galay-ssl/galay-ssl/async/Awaitable.cc
```

Expected: confirm that SSL recv/handshake/shutdown are the complex state-machine-shaped call sites.

**Step 2: Do not migrate downstream in the same patch**

Keep downstream validation as a separate implementation plan or follow-up branch after kernel tests pass.

Expected: kernel feature lands first, downstream migration follows with its own regression scope.

**Step 3: Commit only if you added notes**

```bash
git status --short
```

Expected: no accidental downstream source edits in the kernel branch.

Plan complete and saved to `docs/plans/2026-03-17-awaitable-state-machine-builder.md`. Two execution options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

Which approach?
