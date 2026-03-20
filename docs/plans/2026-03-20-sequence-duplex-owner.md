# Sequence 双工 Owner Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Upgrade `galay-kernel` sequence scheduling from single-owner registration to directional duplex ownership so one read sequence and one write sequence can coexist on the same `IOController`, while bidirectional sequences remain explicitly exclusive.

**Architecture:** Keep reactor hot paths O(1) by introducing fixed read/write sequence-owner slots instead of general owner containers. `SequenceAwaitableBase` records which domain it owns, builders infer read-only or write-only domains when possible, and `StateMachineAwaitable` defaults to bidirectional exclusive ownership unless explicitly proven single-direction.

**Tech Stack:** C++23, coroutine awaitables, `kqueue` / `epoll` / `io_uring`, `IOController`, `SequenceAwaitableBase`, `StateMachineAwaitable`, CMake, socketpair-based regression tests

---

### Task 1: Lock the duplex-owner contract with failing tests

**Files:**
- Modify: `test/T98-sequence_owner_conflict.cc`
- Create: `test/T99-sequence_duplex_split.cc`
- Create: `test/T100-sequence_bidirectional_exclusive.cc`
- Test: `test/T96-state_machine_timeout.cc`
- Test: `test/T97-state_machine_await_context.cc`

**Step 1: Narrow `T98` to the same-direction conflict rule**

Update `test/T98-sequence_owner_conflict.cc` so it clearly asserts:

- first read-domain sequence registers and completes
- second read-domain sequence on the same controller immediately resolves with `kNotReady`

Do not test mixed read/write coexistence in this file anymore.

**Step 2: Write the failing duplex split test**

Create `test/T99-sequence_duplex_split.cc` using one shared `IOController` over a `socketpair(...)` and two sequence awaitables:

- one read-only sequence
- one write-only sequence

Expected behavior:

- both can suspend concurrently on the same controller
- write-side completes after sending one byte
- read-side completes after receiving one byte

**Step 3: Write the failing bidirectional-exclusive test**

Create `test/T100-sequence_bidirectional_exclusive.cc` with:

- one bidirectional `StateMachineAwaitable` that may switch between read and write
- one second single-direction sequence on the same controller

Expected behavior:

- the bidirectional owner registers
- the second owner immediately resolves with `kNotReady`

**Step 4: Run the new red tests**

Run:

```bash
cmake -S . -B build-sequence-duplex-owner -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=OFF -DBUILD_EXAMPLES=OFF
cmake --build build-sequence-duplex-owner --target T98-sequence_owner_conflict T99-sequence_duplex_split T100-sequence_bidirectional_exclusive --parallel 4
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T98-sequence_owner_conflict
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T99-sequence_duplex_split
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T100-sequence_bidirectional_exclusive
```

Expected:

- `T98` should already PASS or stay easy to adapt
- `T99` should FAIL because mixed read/write duplex is not supported yet
- `T100` should FAIL or expose the current single-slot limitation clearly

**Step 5: Commit the failing tests**

```bash
git add test/T98-sequence_owner_conflict.cc test/T99-sequence_duplex_split.cc test/T100-sequence_bidirectional_exclusive.cc
git commit -m "新增 sequence 双工 owner 失败测试"
```

### Task 2: Add directional sequence-owner slots to IOController

**Files:**
- Modify: `galay-kernel/kernel/IOController.hpp`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Modify: `galay-kernel/kernel/Awaitable.h`

**Step 1: Add the owner-domain enum**

In `galay-kernel/kernel/Awaitable.h`, add:

- `enum class SequenceOwnerDomain : uint8_t`
  - `Read`
  - `Write`
  - `ReadWrite`

**Step 2: Extend IOController with fixed sequence-owner slots**

In `galay-kernel/kernel/IOController.hpp`, add:

- `SequenceAwaitableBase* m_sequence_owner[IOController::SIZE] = {nullptr, nullptr};`

Do not replace existing `m_awaitable[READ/WRITE]`; sequence owners must become a parallel fixed-size structure.

**Step 3: Add small helpers in `IOScheduler.hpp`**

Add helpers such as:

- `getSequenceOwner(IOController::Index slot)`
- `setSequenceOwner(IOController::Index slot, SequenceAwaitableBase* owner)`
- `clearSequenceOwner(IOController::Index slot, SequenceAwaitableBase* owner)`

Keep these O(1) and side-effect free beyond slot assignment.

**Step 4: Run build to verify the new fields compile**

Run:

```bash
cmake --build build-sequence-duplex-owner --target galay-kernel --parallel 4
```

Expected: compile may still fail in reactor code until Task 4 lands, but the controller/API pieces should be syntactically correct.

### Task 3: Make SequenceAwaitableBase register and clean up by domain

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T98-sequence_owner_conflict.cc`
- Test: `test/T100-sequence_bidirectional_exclusive.cc`

**Step 1: Record requested and registered domains**

In `SequenceAwaitableBase`, add:

- requested domain
- registered domain
- helpers for:
  - checking slot conflicts
  - claiming slots atomically
  - releasing only owned slots

`onCompleted()` must only clear slots actually owned by `this`.

**Step 2: Update `suspendSequenceAwaitable(...)`**

Change registration so:

- `Read` claims only read owner slot
- `Write` claims only write owner slot
- `ReadWrite` must claim both or fail

If any required slot is occupied by a different owner, return `kNotReady`.

**Step 3: Keep fail-fast for unsupported overlaps**

Explicitly preserve:

- read + read conflict => fail
- write + write conflict => fail
- read/write + readwrite conflict => fail

Do not attempt fairness queues or owner replacement.

**Step 4: Run the conflict tests**

Run:

```bash
cmake --build build-sequence-duplex-owner --target T98-sequence_owner_conflict T100-sequence_bidirectional_exclusive --parallel 4
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T98-sequence_owner_conflict
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T100-sequence_bidirectional_exclusive
```

Expected:

- `T98` PASS
- `T100` still red until reactor dispatch becomes direction-aware

**Step 5: Commit the registration core**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/IOController.hpp galay-kernel/kernel/IOScheduler.hpp test/T98-sequence_owner_conflict.cc test/T100-sequence_bidirectional_exclusive.cc
git commit -m "实现 sequence owner 按方向注册"
```

### Task 4: Teach linear builders and state machines to declare their domain

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T99-sequence_duplex_split.cc`
- Test: `test/T100-sequence_bidirectional_exclusive.cc`

**Step 1: Add domain inference to linear builder machines**

For builder-generated linear sequences:

- only recv/read-like nodes => `Read`
- only send/write-like nodes => `Write`
- mixed read/write or connect => `ReadWrite`

Make this inference happen inside the builder/build path, not in user code.

**Step 2: Give `StateMachineAwaitable` a safe default**

Default `StateMachineAwaitable<MachineT>` to `ReadWrite`.

If you need a customization hook, make it an optional trait or static member checked by `if constexpr`, but keep the default conservative.

**Step 3: Ensure existing SSL-style machines stay exclusive**

Any machine that can switch directions must continue to occupy both slots.

Do not add optimistic runtime direction switching in this task.

**Step 4: Re-run the red tests**

Run:

```bash
cmake --build build-sequence-duplex-owner --target T99-sequence_duplex_split T100-sequence_bidirectional_exclusive --parallel 4
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T99-sequence_duplex_split
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T100-sequence_bidirectional_exclusive
```

Expected:

- `T100` now describes correct exclusivity with no silent corruption
- `T99` may still be red until reactor scheduling and event routing are updated

### Task 5: Make kqueue and epoll sequence scheduling direction-aware

**Files:**
- Modify: `galay-kernel/kernel/KqueueReactor.cc`
- Modify: `galay-kernel/kernel/EpollReactor.cc`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Test: `test/T99-sequence_duplex_split.cc`

**Step 1: Update `addSequence(IOController*)`**

Change the implementation so it:

- checks read owner slot
- checks write owner slot
- calls `prepareForSubmit(...)` separately per existing owner
- combines the needed read/write readiness into one fd registration

**Step 2: Route events back by direction**

On read readiness:

- dispatch only to the read-direction sequence owner

On write readiness:

- dispatch only to the write-direction sequence owner

Do not broadcast a read event to the write owner or vice versa.

**Step 3: Preserve current immediate-ready behavior**

If a sequence completes inline during `prepareForSubmit(...)`, treat it as immediate-ready exactly as before.

**Step 4: Run the duplex split test**

Run:

```bash
cmake --build build-sequence-duplex-owner --target T99-sequence_duplex_split --parallel 4
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T99-sequence_duplex_split
```

Expected: PASS on kqueue/epoll.

**Step 5: Commit the reactor split support**

```bash
git add galay-kernel/kernel/KqueueReactor.cc galay-kernel/kernel/EpollReactor.cc galay-kernel/kernel/IOScheduler.hpp test/T99-sequence_duplex_split.cc
git commit -m "支持 sequence 读写双工调度"
```

### Task 6: Make io_uring sequence scheduling direction-aware

**Files:**
- Modify: `galay-kernel/kernel/IOUringReactor.cc`
- Modify: `galay-kernel/kernel/IOController.hpp`
- Test: `test/T99-sequence_duplex_split.cc`
- Test: `test/T100-sequence_bidirectional_exclusive.cc`

**Step 1: Reuse READ/WRITE token slots for sequence owners**

Update `addSequence(IOController*)` and CQE routing so:

- read sequence owner uses the `READ` SQE token
- write sequence owner uses the `WRITE` SQE token

Do not introduce dynamic token allocation tables.

**Step 2: Submit independent SQEs when both directions are armed**

If both read and write sequence owners are active on one controller, submit one SQE per direction.

Keep ownership and generation checks aligned with existing token behavior.

**Step 3: Route CQE by token slot**

On completion:

- `READ` slot => read sequence owner
- `WRITE` slot => write sequence owner

This must not affect ordinary non-sequence awaitables.

**Step 4: Run the duplex and exclusivity tests**

Run:

```bash
cmake --build build-sequence-duplex-owner --target T99-sequence_duplex_split T100-sequence_bidirectional_exclusive --parallel 4
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T99-sequence_duplex_split
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T100-sequence_bidirectional_exclusive
```

Expected: both PASS on io_uring-capable platforms as well.

### Task 7: Run full kernel regressions for the affected awaitable surface

**Files:**
- Test: `test/T96-state_machine_timeout.cc`
- Test: `test/T97-state_machine_await_context.cc`
- Test: `test/T98-sequence_owner_conflict.cc`
- Test: `test/T99-sequence_duplex_split.cc`
- Test: `test/T100-sequence_bidirectional_exclusive.cc`

**Step 1: Run the focused sequence/state-machine suite**

Run:

```bash
cmake --build build-sequence-duplex-owner --target \
  T96-state_machine_timeout \
  T97-state_machine_await_context \
  T98-sequence_owner_conflict \
  T99-sequence_duplex_split \
  T100-sequence_bidirectional_exclusive \
  --parallel 4

DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T96-state_machine_timeout
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T97-state_machine_await_context
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T98-sequence_owner_conflict
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T99-sequence_duplex_split
DYLD_LIBRARY_PATH=$PWD/build-sequence-duplex-owner/lib ./build-sequence-duplex-owner/bin/T100-sequence_bidirectional_exclusive
```

Expected: all PASS.

**Step 2: Spot-check existing builder/parser regressions if present**

Run the relevant existing sequence/parser tests already used by the repo.

Expected: no regression in builder behavior or parse re-arm semantics.

**Step 3: Commit the verified kernel change**

```bash
git add galay-kernel/kernel galay-kernel/docs/plans/2026-03-20-sequence-duplex-owner-design.md galay-kernel/docs/plans/2026-03-20-sequence-duplex-owner.md test/T96-state_machine_timeout.cc test/T97-state_machine_await_context.cc test/T98-sequence_owner_conflict.cc test/T99-sequence_duplex_split.cc test/T100-sequence_bidirectional_exclusive.cc
git commit -m "实现 sequence 双工 owner 调度"
```

### Task 8: Downstream validation in galay-http and galay-ssl

**Files:**
- Inspect: `../galay-http/galay-http/kernel/http2/Http2StreamManager.h`
- Inspect: `../galay-ssl/galay-ssl/async/SslAwaitableCore.h`

**Step 1: Rebuild downstream repos against the new kernel**

Run the local downstream builds that depend on sequence/state-machine behavior.

**Step 2: Re-run the focused downstream checks**

At minimum:

- `galay-http` H2 / WSS surface checks
- SSL state-machine regression tests if available

**Step 3: Verify there is no semantic mismatch**

Expected:

- split read/write protocol flows can use the duplex model
- TLS state-machine flows remain exclusive and continue to behave correctly

**Step 4: Commit downstream follow-up separately if needed**

Keep kernel and downstream commits separate.

---

Plan complete and saved to `docs/plans/2026-03-20-sequence-duplex-owner.md`. Two execution options:

1. Subagent-Driven (this session) - I dispatch fresh subagent per task, review between tasks, fast iteration
2. Parallel Session (separate) - Open new session with executing-plans, batch execution with checkpoints

Which approach?
