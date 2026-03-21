# Sequence Interest Sync Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Shrink sequence registration overhead by caching controller-level interest state and switching kqueue re-arm from full rebuilds to delta synchronization.

**Architecture:** First lock the new controller-level sequence interest contract with a failing helper test. Then add the cached runtime state and common helper functions in `Awaitable.h` / `IOController.hpp`. Finally update `KqueueReactor` and `EpollReactor` to consume the shared cache and run focused sequence regression tests.

**Tech Stack:** C++23, coroutines, kqueue, epoll, io_uring, CMake

---

### Task 1: Lock sequence interest behavior with a failing helper test

**Files:**
- Create: `test/T103-sequence_interest_sync.cc`
- Test: `test/CMakeLists.txt`

**Step 1: Write the failing test**

Create `test/T103-sequence_interest_sync.cc` to require:

- `detail::collectSequenceInterestMask(IOController*)`
- `detail::syncSequenceInterestMask(IOController*)`
- controller cache reflects read-only, write-only, and split-owner cases

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target T103-sequence_interest_sync --parallel
```

Expected: FAIL because the new sequence interest helpers and controller cache do not exist yet.

### Task 2: Add controller-level sequence interest cache and common helpers

**Files:**
- Modify: `galay-kernel/kernel/IOController.hpp`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T103-sequence_interest_sync.cc`

**Step 1: Add minimal controller runtime fields**

Add:

- `m_sequence_interest_mask`
- `m_sequence_armed_mask`

Keep them internal-only and initialize/reset them in copy/move/reset paths.

**Step 2: Add shared helpers in `Awaitable.h`**

Implement:

- slot-bit helpers
- `sequenceInterestMask(IOEventType)`
- `collectSequenceInterestMask(const IOController*)`
- `syncSequenceInterestMask(IOController*)`
- `clearSequenceInterestMask(IOController*)`

**Step 3: Sync cache on sequence lifecycle transitions**

Update the sequence suspend/completion path so that:

- after `prepareForSubmit(...)` the controller cache matches the current active task
- after owner release the controller cache is recomputed or cleared

**Step 4: Run the helper test**

Run:

```bash
cmake --build build --target T103-sequence_interest_sync --parallel
./build/test/T103-sequence_interest_sync
```

Expected: PASS.

### Task 3: Convert kqueue sequence re-arm to delta synchronization

**Files:**
- Modify: `galay-kernel/kernel/KqueueReactor.h`
- Modify: `galay-kernel/kernel/KqueueReactor.cc`
- Test: `test/T99-sequence_duplex_split.cc`
- Test: `test/T100-sequence_bidirectional_exclusive.cc`

**Step 1: Replace full sequence collect/rebuild path**

Use controller cached interest instead of local `collectSequenceEvents(...)`.

**Step 2: Add delta arm/disarm helper**

Implement a small internal helper that compares:

- current `m_sequence_armed_mask`
- desired `m_sequence_interest_mask`

and only emits changed `EV_ADD | EV_ONESHOT` / `EV_DELETE`.

**Step 3: Clear the fired slot before sequence advance**

When a kqueue event arrives, clear the corresponding bit from `m_sequence_armed_mask`, then advance the owner, resync interest, and re-arm only what is still needed.

**Step 4: Run focused sequence tests**

Run:

```bash
cmake --build build --target T99-sequence_duplex_split T100-sequence_bidirectional_exclusive --parallel
./build/test/T99-sequence_duplex_split
./build/test/T100-sequence_bidirectional_exclusive
```

Expected: PASS.

### Task 4: Align epoll with the shared sequence interest cache

**Files:**
- Modify: `galay-kernel/kernel/EpollReactor.cc`
- Test: `test/T98-sequence_owner_conflict.cc`
- Test: `test/T99-sequence_duplex_split.cc`

**Step 1: Replace duplicated sequence collect path**

Switch `buildEvents()` / `addSequence()` to the shared controller cache.

**Step 2: Keep `m_registered_events` as the final de-dup layer**

Do not change public behavior; only remove repeated per-call sequence scanning.

**Step 3: Run focused epoll-safe sequence regressions**

Run:

```bash
cmake --build build --target T98-sequence_owner_conflict T99-sequence_duplex_split --parallel
./build/test/T98-sequence_owner_conflict
./build/test/T99-sequence_duplex_split
```

Expected: PASS on supported backend.

### Task 5: Finish with hot-path regression coverage

**Files:**
- Test: `test/T39-awaitable_hot_path_helpers.cc`
- Test: `test/T63-custom_sequence_awaitable.cc`
- Test: `test/T87-awaitable_builder_state_machine_bridge.cc`

**Step 1: Build focused hot-path targets**

Run:

```bash
cmake --build build --target T39-awaitable_hot_path_helpers T63-custom_sequence_awaitable T87-awaitable_builder_state_machine_bridge T98-sequence_owner_conflict T99-sequence_duplex_split T100-sequence_bidirectional_exclusive T103-sequence_interest_sync --parallel
```

**Step 2: Run focused hot-path verification**

Run:

```bash
./build/test/T39-awaitable_hot_path_helpers
./build/test/T63-custom_sequence_awaitable
./build/test/T87-awaitable_builder_state_machine_bridge
./build/test/T98-sequence_owner_conflict
./build/test/T99-sequence_duplex_split
./build/test/T100-sequence_bidirectional_exclusive
./build/test/T103-sequence_interest_sync
```

Expected: PASS.
