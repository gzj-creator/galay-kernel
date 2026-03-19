# Awaitable Builder `readv/writev` Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add first-class `readv(...)` and `writev(...)` support to `AwaitableBuilder`, bridge them through `MachineAction` / `StateMachineAwaitable`, and keep the existing single-buffer builder API unchanged.

**Architecture:** Extend the shared state-machine core instead of inventing a new execution path. First lock the new public surface with failing tests, then add runtime iovec context support, then teach `StateMachineAwaitable` and `LinearMachine` to drive `READV/WRITEV`, and finally document the new builder entry points.

**Tech Stack:** C++23, coroutine awaitables, `epoll`, `kqueue`, `io_uring`, CMake, socketpair-based regression tests, Git worktrees

---

### Task 1: Establish a clean baseline and add failing iovec builder tests

**Files:**
- Inspect: `galay-kernel/kernel/Awaitable.h`
- Inspect: `galay-kernel/kernel/Awaitable.cc`
- Inspect: `galay-kernel/kernel/IOScheduler.hpp`
- Inspect: `galay-kernel/async/TcpSocket.h`
- Create: `test/T93-awaitable_builder_iovec_surface.cc`
- Create: `test/T94-awaitable_builder_iovec_roundtrip.cc`
- Create: `test/T95-awaitable_builder_iovec_parse_bridge.cc`

**Step 1: Configure a dedicated build directory**

Run:

```bash
cmake -S . -B build-awaitable-builder-iovec -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
```

Expected: configure succeeds before any code changes.

**Step 2: Write the failing surface test**

Create `test/T93-awaitable_builder_iovec_surface.cc` to require:

- `MachineSignal::kWaitReadv`
- `MachineSignal::kWaitWritev`
- `MachineAction<ResultT>::waitReadv(...)`
- `MachineAction<ResultT>::waitWritev(...)`
- `AwaitableBuilder<Result, 4, Flow>::readv(...)`
- `AwaitableBuilder<Result, 4, Flow>::writev(...)`

The test should use `static_assert` and a tiny `Flow`:

```cpp
struct Flow {
    void onReadv(SequenceOps<Result, 4>& ops, ReadvIOContext& ctx) {
        ops.complete(std::move(ctx.m_result));
    }

    void onWritev(SequenceOps<Result, 4>& ops, WritevIOContext& ctx) {
        ops.complete(std::move(ctx.m_result));
    }
};
```

**Step 3: Write the failing runtime roundtrip test**

Create `test/T94-awaitable_builder_iovec_roundtrip.cc` using `socketpair(...)` or the existing local TCP helper pattern. Build one side with:

```cpp
auto awaitable = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .writev<&Flow::onWritev>(iovecs, 2)
    .build();
```

and the peer with:

```cpp
auto awaitable = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .readv<&Flow::onReadv>(iovecs, 2)
    .build();
```

Expected payload shape: `"HEAD:" + "body-data"`.

**Step 4: Write the failing mixed parse bridge test**

Create `test/T95-awaitable_builder_iovec_parse_bridge.cc` to exercise:

```cpp
auto awaitable = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .readv<&Flow::onReadv>(flow.recv_iovecs, 2)
    .parse<&Flow::onParse>()
    .writev<&Flow::onWritev>(flow.send_iovecs, 2)
    .build();
```

The parse step should require a complete `len + payload` frame before completing.

**Step 5: Run the new tests and verify they fail for the right reason**

Run:

```bash
cmake --build build-awaitable-builder-iovec --target T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip T95-awaitable_builder_iovec_parse_bridge --parallel
```

Expected: FAIL because `MachineSignal`, `MachineAction`, and `AwaitableBuilder` do not yet expose `readv/writev`.

**Step 6: Commit the failing tests**

```bash
git add test/T93-awaitable_builder_iovec_surface.cc test/T94-awaitable_builder_iovec_roundtrip.cc test/T95-awaitable_builder_iovec_parse_bridge.cc
git commit -m "新增 Awaitable Builder iovec 失败测试"
```

### Task 2: Add runtime-span `ReadvIOContext` / `WritevIOContext` support

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Test: `test/T93-awaitable_builder_iovec_surface.cc`

**Step 1: Add runtime-span constructors**

In `galay-kernel/kernel/Awaitable.h`, add:

```cpp
explicit ReadvIOContext(std::span<const struct iovec> iovecs);
explicit WritevIOContext(std::span<const struct iovec> iovecs);
```

Reuse the same validation semantics used by the template borrowed-array constructors.

**Step 2: Keep the current templated constructors intact**

Do not remove the existing overloads for:

- `std::array<struct iovec, N>&`
- `struct iovec (&iovecs)[N]`

They remain part of the public surface.

**Step 3: Implement the minimal compile path**

In `galay-kernel/kernel/Awaitable.cc`, add whatever runtime helpers are needed so `ReadvIOContext` and `WritevIOContext` can be instantiated from `std::span<const struct iovec>`.

**Step 4: Run the surface test to ensure the new contexts compile**

Run:

```bash
cmake --build build-awaitable-builder-iovec --target T93-awaitable_builder_iovec_surface --parallel
```

Expected: it still fails, but only because builder/machine `readv/writev` support is not implemented yet.

**Step 5: Commit the context support**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc
git commit -m "增加 iovec 上下文运行时 span 构造"
```

### Task 3: Extend `MachineSignal` and `StateMachineAwaitable` for `READV/WRITEV`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Inspect: `galay-kernel/kernel/IOScheduler.hpp`
- Inspect: `galay-kernel/kernel/EpollReactor.cc`
- Inspect: `galay-kernel/kernel/KqueueReactor.cc`
- Inspect: `galay-kernel/kernel/IOUringReactor.cc`
- Test: `test/T93-awaitable_builder_iovec_surface.cc`
- Test: `test/T94-awaitable_builder_iovec_roundtrip.cc`

**Step 1: Add new machine signals**

In `MachineSignal`, add:

- `kWaitReadv`
- `kWaitWritev`

**Step 2: Extend `MachineAction<ResultT>`**

Add iovec fields:

```cpp
const struct iovec* iovecs = nullptr;
size_t iov_count = 0;
```

and factory helpers:

```cpp
static MachineAction waitReadv(const struct iovec* iovecs, size_t count);
static MachineAction waitWritev(const struct iovec* iovecs, size_t count);
```

**Step 3: Extend `StateMachineAwaitable` active task storage**

Add:

- `ActiveKind::kReadv`
- `ActiveKind::kWritev`
- `ReadvIOContext m_readv_context`
- `WritevIOContext m_writev_context`

**Step 4: Teach `pump()` to bridge the new actions**

Map:

- `kWaitReadv -> IOTask{READV, nullptr, &m_readv_context}`
- `kWaitWritev -> IOTask{WRITEV, nullptr, &m_writev_context}`

and on completion feed the result back through the existing:

- `machine.onRead(std::expected<size_t, IOError>)`
- `machine.onWrite(std::expected<size_t, IOError>)`

Do not change the state-machine concept signatures in this task.

**Step 5: Run the surface and roundtrip tests**

Run:

```bash
cmake --build build-awaitable-builder-iovec --target T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip --parallel
./build-awaitable-builder-iovec/bin/T93-awaitable_builder_iovec_surface
```

Expected: `T93` passes; `T94` still fails until builder nodes exist.

**Step 6: Commit the state-machine bridge**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc
git commit -m "扩展状态机 Awaitable 支持 iovec 动作"
```

### Task 4: Add `readv/writev` nodes to `LinearMachine` and `AwaitableBuilder`

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T93-awaitable_builder_iovec_surface.cc`
- Test: `test/T94-awaitable_builder_iovec_roundtrip.cc`
- Test: `test/T95-awaitable_builder_iovec_parse_bridge.cc`

**Step 1: Extend `LinearMachine::NodeKind` and node storage**

Add:

- `kReadv`
- `kWritev`

and store:

- `const struct iovec* iovecs`
- `size_t iov_count`

**Step 2: Add node constructors**

Add:

```cpp
template <auto Handler>
static Node makeReadvNode(const struct iovec* iovecs, size_t count);

template <auto Handler>
static Node makeWritevNode(const struct iovec* iovecs, size_t count);
```

The handlers must dispatch to:

- `ReadvIOContext`
- `WritevIOContext`

**Step 3: Add public builder entry points**

In `AwaitableBuilder`, add the four overloads:

```cpp
template <auto Handler, size_t N>
AwaitableBuilder& readv(std::array<struct iovec, N>& iovecs, size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& readv(struct iovec (&iovecs)[N], size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& writev(std::array<struct iovec, N>& iovecs, size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& writev(struct iovec (&iovecs)[N], size_t count = N);
```

These should push `kReadv/kWritev` nodes, not fall back to `.recv/.send`.

**Step 4: Extend `advance()` to emit the new machine actions**

Map:

- `kReadv -> MachineAction::waitReadv(node.iovecs, node.iov_count)`
- `kWritev -> MachineAction::waitWritev(node.iovecs, node.iov_count)`

**Step 5: Run the focused tests and verify green**

Run:

```bash
cmake --build build-awaitable-builder-iovec --target T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip T95-awaitable_builder_iovec_parse_bridge --parallel
./build-awaitable-builder-iovec/bin/T93-awaitable_builder_iovec_surface
./build-awaitable-builder-iovec/bin/T94-awaitable_builder_iovec_roundtrip
./build-awaitable-builder-iovec/bin/T95-awaitable_builder_iovec_parse_bridge
```

Expected: all three PASS.

**Step 6: Commit the builder surface**

```bash
git add galay-kernel/kernel/Awaitable.h test/T93-awaitable_builder_iovec_surface.cc test/T94-awaitable_builder_iovec_roundtrip.cc test/T95-awaitable_builder_iovec_parse_bridge.cc
git commit -m "为 Awaitable Builder 增加 readv writev"
```

### Task 5: Run full regression on existing awaitable and iovec surfaces

**Files:**
- Test: `test/T19-readv_writev.cc`
- Test: `test/T85-state_machine_awaitable_surface.cc`
- Test: `test/T86-state_machine_read_write_loop.cc`
- Test: `test/T87-awaitable_builder_state_machine_bridge.cc`
- Test: `test/T89-state_machine_zero_length_actions.cc`
- Test: `test/T90-awaitable_builder_connect_bridge.cc`
- Test: `test/T92-awaitable_builder_queue_rejected.cc`
- Test: `test/T93-awaitable_builder_iovec_surface.cc`
- Test: `test/T94-awaitable_builder_iovec_roundtrip.cc`
- Test: `test/T95-awaitable_builder_iovec_parse_bridge.cc`

**Step 1: Build the regression targets**

Run:

```bash
cmake --build build-awaitable-builder-iovec --target T19-readv_writev T85-state_machine_awaitable_surface T86-state_machine_read_write_loop T87-awaitable_builder_state_machine_bridge T89-state_machine_zero_length_actions T90-awaitable_builder_connect_bridge T92-awaitable_builder_queue_rejected T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip T95-awaitable_builder_iovec_parse_bridge --parallel
```

Expected: build succeeds.

**Step 2: Run the pure unit-style targets**

Run:

```bash
./build-awaitable-builder-iovec/bin/T19-readv_writev
./build-awaitable-builder-iovec/bin/T85-state_machine_awaitable_surface
./build-awaitable-builder-iovec/bin/T86-state_machine_read_write_loop
./build-awaitable-builder-iovec/bin/T87-awaitable_builder_state_machine_bridge
./build-awaitable-builder-iovec/bin/T89-state_machine_zero_length_actions
./build-awaitable-builder-iovec/bin/T90-awaitable_builder_connect_bridge
./build-awaitable-builder-iovec/bin/T92-awaitable_builder_queue_rejected
./build-awaitable-builder-iovec/bin/T93-awaitable_builder_iovec_surface
./build-awaitable-builder-iovec/bin/T94-awaitable_builder_iovec_roundtrip
./build-awaitable-builder-iovec/bin/T95-awaitable_builder_iovec_parse_bridge
```

Expected: all PASS.

**Step 3: Investigate any regression before changing docs**

If any target fails, fix code first. Do not update docs while tests are red.

**Step 4: Commit the regression pass if any follow-up fixes were required**

```bash
git status --short
```

Expected: clean or only intentional pending doc changes remain.

### Task 6: Update docs and record the new builder public surface

**Files:**
- Modify: `README.md`
- Modify: `docs/02-API参考.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/06-高级主题.md`

**Step 1: Update the API reference**

In `docs/02-API参考.md`, document the new builder methods:

- `.readv<&Flow::onReadv>(iovecs, count)`
- `.writev<&Flow::onWritev>(iovecs, count)`
- `MachineAction::waitReadv(...)`
- `MachineAction::waitWritev(...)`

**Step 2: Update the usage guide**

In `docs/03-使用指南.md`, add a minimal builder example that sends two buffers with `writev(...)` and receives into two buffers with `readv(...)`.

**Step 3: Update advanced usage guidance**

In `docs/06-高级主题.md`, clarify:

- 线性多段 IO 优先 builder `readv/writev`
- 复杂协议切换仍优先 `fromStateMachine(...)`

**Step 4: Update the README summary**

Add one short bullet in `README.md` so the public-facing entry page also reflects builder `readv/writev`.

**Step 5: Re-run one focused doc-backed regression**

Run:

```bash
./build-awaitable-builder-iovec/bin/T94-awaitable_builder_iovec_roundtrip
```

Expected: still PASS after doc-only changes.

**Step 6: Commit the docs update**

```bash
git add README.md docs/02-API参考.md docs/03-使用指南.md docs/06-高级主题.md
git commit -m "文档: 补充 Awaitable Builder iovec 用法"
```

### Task 7: Prepare the kernel handoff for upper-layer migration

**Files:**
- Modify: `docs/plans/2026-03-19-awaitable-builder-readv-writev-design.md`
- Modify: `docs/plans/2026-03-19-awaitable-builder-readv-writev.md`

**Step 1: Record final verification results**

At the bottom of both plan docs, append the commands actually run and whether they passed.

**Step 2: Record upper-layer follow-up**

Add a short note that `galay-http` should migrate header/body split sends to builder `writev(...)` once the kernel change is merged or consumed via submodule update.

**Step 3: Commit the handoff note**

```bash
git add docs/plans/2026-03-19-awaitable-builder-readv-writev-design.md docs/plans/2026-03-19-awaitable-builder-readv-writev.md
git commit -m "文档: 记录 Awaitable Builder iovec 实施结果"
```

## 最终执行记录（2026-03-19）

已完成提交：

- `aeb4027` `新增 Awaitable Builder iovec 失败测试`
- `4599523` `增加 iovec 上下文运行时 span 构造`
- `592405b` `扩展状态机 Awaitable 支持 iovec 动作`
- `75557d6` `为 Awaitable Builder 增加 readv writev`
- `0d85de6` `文档: 补充 Awaitable Builder iovec 用法`

fresh 验证命令：

```bash
cmake --build build-awaitable-builder-iovec --target T19-readv_writev T85-state_machine_awaitable_surface T86-state_machine_read_write_loop T87-awaitable_builder_state_machine_bridge T89-state_machine_zero_length_actions T90-awaitable_builder_connect_bridge T92-awaitable_builder_queue_rejected T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip T95-awaitable_builder_iovec_parse_bridge --parallel

for name in T19-readv_writev T85-state_machine_awaitable_surface T86-state_machine_read_write_loop T87-awaitable_builder_state_machine_bridge T89-state_machine_zero_length_actions T90-awaitable_builder_connect_bridge T92-awaitable_builder_queue_rejected T93-awaitable_builder_iovec_surface T94-awaitable_builder_iovec_roundtrip T95-awaitable_builder_iovec_parse_bridge; do
  ./build-awaitable-builder-iovec/bin/$name
done

for i in 1 2 3 4 5; do
  ./build-awaitable-builder-iovec/bin/T94-awaitable_builder_iovec_roundtrip
  ./build-awaitable-builder-iovec/bin/T95-awaitable_builder_iovec_parse_bridge
done
```

执行结果：

- build 成功
- `T19/T85/T86/T87/T89/T90/T92/T93/T94/T95` 全部 PASS
- `T94/T95` 稳定性补跑 5 轮，全部 PASS

上层迁移备注：

- `galay-http` 合并本轮 kernel 后，header/body 分段发送优先改用 builder `writev(...)`
- `galay-http` 需要半包解析前的多段读时，可直接使用 `readv(...).parse(...)`
- 其他 `galay-*` 协议库如已有私有 scatter-gather awaitable，优先收敛到统一 builder 表达
