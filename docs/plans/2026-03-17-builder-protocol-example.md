# Builder Protocol Example Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal official Builder protocol example that demonstrates `recv -> parse -> send` with `ByteQueueView` and `ParseStatus::kNeedMore`.

**Architecture:** Add one self-contained `socketpair(...)`-based example in both `examples/include/` and `examples/import/`. The include example must be fully buildable in the current environment; the import example should mirror it for module users and be documented as conditional on module toolchain support.

**Tech Stack:** C++23, coroutine tasks, `AwaitableBuilder`, `ByteQueueView`, `ParseStatus`, `socketpair`, CMake

---

### Task 1: Register the new example targets first

**Files:**
- Modify: `examples/CMakeLists.txt`

**Step 1: Add failing target declarations**

Register:

- `E11-BuilderProtocol`
- `E11-BuilderProtocolImport`

with source paths:

- `examples/include/E11-builder_protocol.cc`
- `examples/import/E11-builder_protocol.cc`

**Step 2: Run configure/build to verify RED**

Run:

```bash
cmake -S . -B build-awaitable-state-machine
cmake --build build-awaitable-state-machine --target E11-BuilderProtocol --parallel
```

Expected: FAIL because the new source file does not exist yet.

### Task 2: Add the include example

**Files:**
- Create: `examples/include/E11-builder_protocol.cc`

**Step 1: Implement the failing protocol shape**

Create a `Flow` with:

- `onRecv(...)`
- `onParse(...)`
- `onSend(...)`

and a builder:

```cpp
auto awaitable = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .recv<&Flow::onRecv>(flow.scratch, sizeof(flow.scratch))
    .parse<&Flow::onParse>()
    .send<&Flow::onSend>(flow.reply.data(), flow.reply.size())
    .build();
```

The peer should send the length-prefixed `ping` frame in two chunks so the example depends on `ParseStatus::kNeedMore`.

**Step 2: Run the include target**

Run:

```bash
cmake --build build-awaitable-state-machine --target E11-BuilderProtocol --parallel
./build-awaitable-state-machine/bin/E11-BuilderProtocol
```

Expected: PASS.

### Task 3: Add the import example

**Files:**
- Create: `examples/import/E11-builder_protocol.cc`

**Step 1: Mirror the include example with module imports**

Use:

```cpp
import galay.kernel;
```

Keep the same protocol behavior and output expectations.

**Step 2: Verify target visibility**

Run:

```bash
cmake --build build-awaitable-state-machine --target help | rg "E11-BuilderProtocol"
```

Expected:

- include target appears
- import target appears only when module toolchain support is actually enabled

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

Document the matching import target and current environment note.

### Task 5: Final verification

**Files:**
- Inspect: `examples/CMakeLists.txt`
- Inspect: `examples/include/E11-builder_protocol.cc`
- Inspect: `examples/import/E11-builder_protocol.cc`
- Inspect: `docs/04-示例代码.md`

**Step 1: Run fresh verification**

Run:

```bash
cmake --build build-awaitable-state-machine --target E11-BuilderProtocol --parallel
./build-awaitable-state-machine/bin/E11-BuilderProtocol
cmake --build build-awaitable-state-machine --target help | rg "E11-BuilderProtocol"
```

**Step 2: Check the diff**

Run:

```bash
git diff --check
git diff --stat
```

Expected: no whitespace errors; only intended example/docs files changed.
