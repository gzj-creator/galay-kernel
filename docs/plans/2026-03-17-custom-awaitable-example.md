# Custom Awaitable Example Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal official `examples/` demonstration showing how users build a custom awaitable with the new state-machine model.

**Architecture:** Add one tiny self-contained `socketpair(...)`-based example in both `examples/include/` and `examples/import/`. The example will define a small state machine, build it through `AwaitableBuilder<Result>::fromStateMachine(...)`, and complete a local `ping -> pong` round trip without external network dependencies.

**Tech Stack:** C++23, coroutine tasks, `AwaitableBuilder`, `MachineAction`, `socketpair`, CMake

---

### Task 1: Register the new example targets first

**Files:**
- Modify: `examples/CMakeLists.txt`

**Step 1: Add failing target declarations**

Register:

- `E10-CustomAwaitable`
- `E10-CustomAwaitableImport`

with source paths:

- `examples/include/E10-custom_awaitable.cc`
- `examples/import/E10-custom_awaitable.cc`

**Step 2: Run configure/build to verify RED**

Run:

```bash
cmake -S . -B build-awaitable-state-machine
cmake --build build-awaitable-state-machine --target E10-CustomAwaitable --parallel
```

Expected: FAIL because the new source file does not exist yet.

### Task 2: Add the include example

**Files:**
- Create: `examples/include/E10-custom_awaitable.cc`

**Step 1: Implement the minimal state machine example**

Add:

- `PingPongMachine`
- `Task<void>` that constructs the machine-backed awaitable
- `socketpair(...)`-based peer thread
- final pass/fail output

**Step 2: Run the include target**

Run:

```bash
cmake --build build-awaitable-state-machine --target E10-CustomAwaitable --parallel
./build-awaitable-state-machine/bin/E10-CustomAwaitable
```

Expected: PASS.

### Task 3: Add the import example

**Files:**
- Create: `examples/import/E10-custom_awaitable.cc`

**Step 1: Mirror the include example with module imports**

Keep the same state machine and self-contained behavior, but use:

```cpp
import galay.kernel;
```

**Step 2: Run the import target when available**

Run:

```bash
cmake --build build-awaitable-state-machine --target E10-CustomAwaitableImport --parallel
./build-awaitable-state-machine/bin/E10-CustomAwaitableImport
```

Expected: PASS when module targets are available in the current toolchain.

### Task 4: Update example index documentation

**Files:**
- Modify: `docs/04-示例代码.md`

**Step 1: Add the include example row**

Document:

- source path
- target
- execution mode
- command
- current status

**Step 2: Add the import example row**

Document the matching import target and the same execution notes.

### Task 5: Final verification

**Files:**
- Inspect: `examples/CMakeLists.txt`
- Inspect: `examples/include/E10-custom_awaitable.cc`
- Inspect: `examples/import/E10-custom_awaitable.cc`
- Inspect: `docs/04-示例代码.md`

**Step 1: Run fresh verification**

Run:

```bash
cmake --build build-awaitable-state-machine --target \
  E10-CustomAwaitable E10-CustomAwaitableImport --parallel

./build-awaitable-state-machine/bin/E10-CustomAwaitable
./build-awaitable-state-machine/bin/E10-CustomAwaitableImport
```

If the import target is unavailable in the current environment, record that explicitly and still verify the include target.

**Step 2: Check the diff**

Run:

```bash
git diff --check
git diff --stat
```

Expected: no whitespace errors; only the intended example/docs files changed.
