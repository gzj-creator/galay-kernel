# IO API Unification Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将 `galay-kernel` 的 I/O 主接口统一到高性能语义：读接口统一返回 `size_t`，vectored I/O 只保留 borrowed `iovec`，删除旧的 `Bytes` / `span` 兼容层。

**Architecture:** 先通过测试和调用点迁移把旧接口编译面收窄，再修改 `IOHandlers` 和 `Awaitable` 的返回类型，最后切换公开 API 与 benchmark/doc 示例。统一后的主数据流始终是“调用方持有缓冲区，框架返回实际字节数”，不再在热路径里包装 `Bytes` 或复制 `iovec` 元数据。

**Tech Stack:** C++23, coroutines, kqueue, epoll, io_uring, `std::expected`, borrowed `iovec`, CMake

---

### Task 1: 先让测试和 benchmark 切到新语义

**Files:**
- Modify: `test/T2-TcpSocket.cc`
- Modify: `test/T4-TcpClient.cc`
- Modify: `test/T5-UdpSocket.cc`
- Modify: `test/T6-UdpServer.cc`
- Modify: `test/T8-FileIo.cc`
- Modify: `test/T10-Timeout.cc`
- Modify: `test/T19-ReadvWritev.cc`
- Modify: `test/T20-RingbufferIo.cc`
- Modify: `test/T22-Sendfile.cc`
- Modify: `test/T23-SendfileBasic.cc`
- Modify: `test/T26-ConcurrentRecvSend.cc`
- Modify: `benchmark/B2-TcpServer.cc`
- Modify: `benchmark/B3-TcpClient.cc`
- Modify: `benchmark/B11-TcpIovServer.cc`
- Modify: `benchmark/B12-TcpIovClient.cc`

**Step 1: Write the failing test changes**

把所有读调用改成新语义，先不改实现：

```cpp
auto recvResult = co_await client.recv(buffer, sizeof(buffer));
ASSERT_TRUE(recvResult.has_value());
size_t n = recvResult.value();
std::string_view view(buffer, n);
```

把所有 `bytes.c_str()` / `bytes.size()` 改成直接 `buffer + n`：

```cpp
size_t n = recvResult.value();
auto sendResult = co_await client.send(buffer, n);
```

把所有 `readv/writev(std::span<const iovec>)` 调用点替换成 borrowed 数组版本：

```cpp
std::array<struct iovec, 2> iovecs{};
size_t count = fillIovecs(iovecs);
auto result = co_await client.readv(iovecs, count);
```

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target \
  T2-TcpSocket T4-TcpClient T5-UdpSocket T6-UdpServer T8-FileIo \
  T10-Timeout T19-ReadvWritev T20-RingbufferIo T22-Sendfile \
  T23-SendfileBasic T26-ConcurrentRecvSend \
  B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4
```

Expected: FAIL，错误集中在：
- `recv/recvfrom/read` 返回 `Bytes` 的旧调用方式
- `readv/writev(std::span<const iovec>)` 不再匹配

**Step 3: Keep semantics explicit in the test code**

所有文本比较统一写成：

```cpp
std::string_view payload(buffer, n);
```

不要在测试里再引入 `Bytes` 包装。

**Step 4: Run build again to confirm failures are only API-shape failures**

Run:

```bash
cmake --build build --target \
  T2-TcpSocket T5-UdpSocket T8-FileIo T19-ReadvWritev B2-TcpServer B11-TcpIovServer -j4
```

Expected: FAIL，且不出现与本任务无关的新错误。

**Step 5: Commit**

```bash
git add \
  test/T2-TcpSocket.cc test/T4-TcpClient.cc test/T5-UdpSocket.cc test/T6-UdpServer.cc \
  test/T8-FileIo.cc test/T10-Timeout.cc test/T19-ReadvWritev.cc test/T20-RingbufferIo.cc \
  test/T22-Sendfile.cc test/T23-SendfileBasic.cc test/T26-ConcurrentRecvSend.cc \
  benchmark/B2-TcpServer.cc benchmark/B3-TcpClient.cc \
  benchmark/B11-TcpIovServer.cc benchmark/B12-TcpIovClient.cc
git commit -m "test: migrate IO call sites to size_t semantics"
```

### Task 2: 修改 IOHandlers 和 Awaitable 返回类型

**Files:**
- Modify: `galay-kernel/kernel/IOHandlers.hpp`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Modify: `galay-kernel/kernel/Awaitable.inl`

**Step 1: Write the failing interface expectations**

把以下结果类型统一改为 `std::expected<size_t, IOError>`：

```cpp
struct RecvIOContext {
    std::expected<size_t, IOError> m_result;
};

struct RecvFromIOContext {
    std::expected<size_t, IOError> m_result;
};

struct FileReadIOContext {
    std::expected<size_t, IOError> m_result;
};
```

`await_resume()` 也统一返回：

```cpp
std::expected<size_t, IOError> await_resume();
```

**Step 2: Run build to verify failures remain until handlers are updated**

Run:

```bash
cmake --build build --target T2-TcpSocket T5-UdpSocket T8-FileIo T10-Timeout -j4
```

Expected: FAIL，错误集中在 `IOHandlers.hpp` / `Awaitable.cc` 的返回类型不匹配。

**Step 3: Write minimal implementation**

实现目标：

```cpp
inline std::expected<size_t, IOError> handleRecv(GHandle handle, char* buffer, size_t length) {
    ssize_t recvBytes = recv(handle.fd, buffer, length, 0);
    if (recvBytes >= 0) {
        return static_cast<size_t>(recvBytes);
    }
    ...
}
```

`handleRecvFrom()` 统一返回：

```cpp
inline std::pair<std::expected<size_t, IOError>, Host> handleRecvFrom(...);
```

`handleFileRead()` 统一返回：

```cpp
inline std::expected<size_t, IOError> handleFileRead(...);
```

注意：
- TCP `recv == 0` 仍表示关闭
- UDP `recvfrom == 0` 是合法空包
- 文件读 `read == 0` 是 EOF

**Step 4: Run focused tests**

Run:

```bash
cmake --build build --target T2-TcpSocket T5-UdpSocket T8-FileIo T10-Timeout -j4
./build/bin/T2-TcpSocket
./build/bin/T5-UdpSocket
./build/bin/T8-FileIo
./build/bin/T10-Timeout
```

Expected: PASS

**Step 5: Commit**

```bash
git add galay-kernel/kernel/IOHandlers.hpp galay-kernel/kernel/Awaitable.h \
  galay-kernel/kernel/Awaitable.cc galay-kernel/kernel/Awaitable.inl
git commit -m "refactor(io): return size_t from read awaitables"
```

### Task 3: 删除 span / owned fallback，统一 borrowed iovec

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/async/TcpSocket.h`
- Modify: `test/T19-ReadvWritev.cc`
- Modify: `test/T20-RingbufferIo.cc`
- Test: `test/T34-ReadvArrayBorrowed.cc`
- Test: `test/T35-ReadvArrayCountGuard.cc`

**Step 1: Write the failing interface changes**

删除：

```cpp
ReadvAwaitable readv(std::span<const struct iovec> iovecs);
WritevAwaitable writev(std::span<const struct iovec> iovecs);
std::vector<struct iovec> m_owned_iovecs;
```

只保留 borrowed 版本：

```cpp
template<size_t N>
ReadvAwaitable readv(std::array<struct iovec, N>& iovecs, size_t count = N);

template<size_t N>
ReadvAwaitable readv(struct iovec (&iovecs)[N], size_t count = N);
```

必要时补充：

```cpp
ReadvAwaitable readv(struct iovec* iovecs, size_t count);
WritevAwaitable writev(const struct iovec* iovecs, size_t count);
```

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target T19-ReadvWritev T20-RingbufferIo T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard -j4
```

Expected: FAIL，错误只来自删除 `span` / owned fallback 后的接口不匹配。

**Step 3: Write minimal implementation**

核心约束：

```cpp
std::span<const struct iovec> m_iovecs;
```

只作为 borrowed view，不再分配 owned `vector`。

保留 count 校验：

```cpp
if (count > N) {
    std::abort();
}
```

**Step 4: Run focused tests**

Run:

```bash
cmake --build build --target T19-ReadvWritev T20-RingbufferIo T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard -j4
./build/bin/T19-ReadvWritev
./build/bin/T20-RingbufferIo
./build/bin/T34-ReadvArrayBorrowed
./build/bin/T35-ReadvArrayCountGuard
```

Expected: PASS

**Step 5: Commit**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/async/TcpSocket.h \
  test/T19-ReadvWritev.cc test/T20-RingbufferIo.cc \
  test/T34-ReadvArrayBorrowed.cc test/T35-ReadvArrayCountGuard.cc
git commit -m "refactor(io): keep only borrowed vectored IO interfaces"
```

### Task 4: 切换公开 TCP / UDP / AsyncFile API

**Files:**
- Modify: `galay-kernel/async/TcpSocket.h`
- Modify: `galay-kernel/async/UdpSocket.h`
- Modify: `galay-kernel/async/UdpSocket.cc`
- Modify: `galay-kernel/async/AsyncFile.h`
- Modify: `galay-kernel/async/AsyncFile.cc`
- Modify: `benchmark/B2-TcpServer.cc`
- Modify: `benchmark/B3-TcpClient.cc`
- Modify: `benchmark/B11-TcpIovServer.cc`
- Modify: `benchmark/B12-TcpIovClient.cc`

**Step 1: Write the failing public API declarations**

改成：

```cpp
RecvAwaitable recv(char* buffer, size_t length);
RecvFromAwaitable recvfrom(char* buffer, size_t length, Host* from);
FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);
```

但这些 awaitable 的 `await_resume()` 已经返回 `std::expected<size_t, IOError>`。

所有文档注释同步改为“返回读取字节数”，不再写“返回 Bytes”。

**Step 2: Run build to verify declaration / definition consistency**

Run:

```bash
cmake --build build --target \
  T2-TcpSocket T5-UdpSocket T8-FileIo \
  B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4
```

Expected: FAIL，直到头文件注释、定义和调用全部一致。

**Step 3: Write minimal implementation**

典型迁移：

```cpp
auto recvResult = co_await client.recv(buffer, sizeof(buffer));
size_t n = recvResult.value();
auto sendResult = co_await client.send(buffer, n);
```

文件读回验证：

```cpp
size_t n = readResult.value();
std::string_view readBack(buffer, n);
```

**Step 4: Run focused tests and benchmarks**

Run:

```bash
cmake --build build --target \
  T2-TcpSocket T5-UdpSocket T8-FileIo T26-ConcurrentRecvSend \
  B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4
./build/bin/T2-TcpSocket
./build/bin/T5-UdpSocket
./build/bin/T8-FileIo
./build/bin/T26-ConcurrentRecvSend
```

Expected: PASS

**Step 5: Commit**

```bash
git add galay-kernel/async/TcpSocket.h galay-kernel/async/UdpSocket.h \
  galay-kernel/async/UdpSocket.cc galay-kernel/async/AsyncFile.h galay-kernel/async/AsyncFile.cc \
  benchmark/B2-TcpServer.cc benchmark/B3-TcpClient.cc \
  benchmark/B11-TcpIovServer.cc benchmark/B12-TcpIovClient.cc
git commit -m "refactor(api): unify public IO interfaces on size_t semantics"
```

### Task 5: 清理文档与全量本地回归

**Files:**
- Modify: `docs/00-快速开始.md`
- Modify: `docs/05-性能测试.md`
- Modify: `docs/06-高级主题.md`
- Modify: `docs/09-UDP性能测试.md`
- Modify: `docs/14-并发.md`

**Step 1: Update examples to the new API**

所有示例统一改成：

```cpp
char buffer[1024];
auto result = co_await socket.recv(buffer, sizeof(buffer));
size_t n = result.value();
std::string_view payload(buffer, n);
```

不要再出现：

```cpp
auto& bytes = result.value();
bytes.toStringView();
bytes.c_str();
```

**Step 2: Run local regression build**

Run:

```bash
cmake --build build --target \
  T2-TcpSocket T4-TcpClient T5-UdpSocket T6-UdpServer T8-FileIo \
  T10-Timeout T19-ReadvWritev T20-RingbufferIo T22-Sendfile T23-SendfileBasic \
  T26-ConcurrentRecvSend T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard \
  T36-TaskCoreWakePath T41-TaskRefSchedulePath \
  B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4
```

Expected: build PASS

**Step 3: Run local regression runtime**

Run:

```bash
./build/bin/T2-TcpSocket
./build/bin/T5-UdpSocket
./build/bin/T6-UdpServer
./build/bin/T8-FileIo
./build/bin/T10-Timeout
./build/bin/T19-ReadvWritev
./build/bin/T20-RingbufferIo
./build/bin/T22-Sendfile
./build/bin/T23-SendfileBasic
./build/bin/T26-ConcurrentRecvSend
./build/bin/T34-ReadvArrayBorrowed
./build/bin/T35-ReadvArrayCountGuard
./build/bin/T36-TaskCoreWakePath
./build/bin/T41-TaskRefSchedulePath
```

Expected: PASS

**Step 4: Commit**

```bash
git add docs/00-快速开始.md docs/05-性能测试.md docs/06-高级主题.md docs/09-UDP性能测试.md docs/14-并发.md
git commit -m "docs: update examples for unified IO APIs"
```

### Task 6: 远端 Linux Release 验证

**Files:**
- Test: `benchmark/B2-TcpServer.cc`
- Test: `benchmark/B3-TcpClient.cc`
- Test: `benchmark/B11-TcpIovServer.cc`
- Test: `benchmark/B12-TcpIovClient.cc`
- Test: `test/T30-CustomAwaitable.cc`
- Test: `test/T40-IOUringCustomAwaitableNoNullProbe.cc`
- Test: `test/T41-TaskRefSchedulePath.cc`

**Step 1: Sync the worktree to the remote verification host**

Run:

```bash
WT=/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/io-scheduler-worker-redesign
RUNNER=$(cat /tmp/galay_expect_runner_path.txt)
PASS='gzjIlyj520.'
HOST=140.143.142.251
USER=ubuntu
REMOTE_DIR=$(cat /tmp/galay_remote_dir.txt)
"$RUNNER" "$PASS" rsync -az --delete --exclude build --exclude build-* --exclude .git "$WT/" "$USER@$HOST:$REMOTE_DIR/"
```

Expected: sync PASS

**Step 2: Build and verify remote epoll Release**

Run:

```bash
cmake -S "$REMOTE_DIR" -B "$REMOTE_DIR/build-epoll" -DCMAKE_BUILD_TYPE=Release
cmake --build "$REMOTE_DIR/build-epoll" --target \
  T41-TaskRefSchedulePath B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j2
```

Expected: PASS

然后运行短 benchmark，记录 `plain/iov`：

```bash
$REMOTE_DIR/build-epoll/bin/B2-TcpServer 19080
$REMOTE_DIR/build-epoll/bin/B3-TcpClient -h 127.0.0.1 -p 19080 -c 32 -s 256 -d 2
```

**Step 3: Build and verify remote io_uring Release**

Run:

```bash
cmake -S "$REMOTE_DIR" -B "$REMOTE_DIR/build-iouring" -DDISABLE_IOURING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$REMOTE_DIR/build-iouring" --target \
  T30-CustomAwaitable T40-IOUringCustomAwaitableNoNullProbe T41-TaskRefSchedulePath \
  B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j2
```

Expected: PASS

然后运行短 benchmark，记录 `plain/iov`。

**Step 4: Summarize remaining deltas**

必须记录：
- `epoll plain`
- `epoll iov`
- `io_uring plain`
- `io_uring iov`

并明确说明：
- 统一接口后是否消除了 plain 路径里 `Bytes` 包装噪音
- 若仍与 Rust/Go 有差距，下一步瓶颈是否还在 `Waker/TaskRef` 热路径

**Step 5: Commit**

```bash
git add \
  galay-kernel/kernel/IOHandlers.hpp galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.cc galay-kernel/kernel/Awaitable.inl \
  galay-kernel/async/TcpSocket.h galay-kernel/async/UdpSocket.h galay-kernel/async/UdpSocket.cc \
  galay-kernel/async/AsyncFile.h galay-kernel/async/AsyncFile.cc \
  test/T2-TcpSocket.cc test/T4-TcpClient.cc test/T5-UdpSocket.cc test/T6-UdpServer.cc \
  test/T8-FileIo.cc test/T10-Timeout.cc test/T19-ReadvWritev.cc test/T20-RingbufferIo.cc \
  test/T22-Sendfile.cc test/T23-SendfileBasic.cc test/T26-ConcurrentRecvSend.cc \
  test/T34-ReadvArrayBorrowed.cc test/T35-ReadvArrayCountGuard.cc \
  benchmark/B2-TcpServer.cc benchmark/B3-TcpClient.cc benchmark/B11-TcpIovServer.cc benchmark/B12-TcpIovClient.cc \
  docs/00-快速开始.md docs/05-性能测试.md docs/06-高级主题.md docs/09-UDP性能测试.md docs/14-并发.md
git commit -m "refactor(io): unify APIs on borrowed buffers and size_t results"
```
