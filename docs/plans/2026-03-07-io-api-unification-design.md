# IO API Unification Design

**背景**

当前 `galay-kernel` 的 I/O 热路径已经做过一轮 awaitable / scheduler flatten，但公开接口和内部实现仍然同时保留了两套风格：

1. 面向性能的 borrowed buffer / borrowed iovec 接口
2. 面向易用性的 `Bytes` / `std::span<const iovec>` 接口

这种并存会带来两个问题：

1. 框架内部和 benchmark 仍可能走到较厚的接口，导致热路径被对象包装、fallback 拷贝和额外分支拖慢。
2. 对外 API 会继续引导用户使用非高性能路径，后续即使内部继续优化，调用侧也会重复走低效语义。

用户已经确认本轮目标是激进收敛：不保留兼容层，框架内部和对外 API 都统一切到高性能接口。

**目标**

1. 统一 TCP / UDP / AsyncFile 读路径接口，全部改为 borrowed buffer + `size_t` 返回。
2. 统一 vectored I/O 接口，只保留 borrowed `iovec` 版本。
3. 删除框架内部和公开 API 中的低性能兼容层，不保留旧语义转发。
4. 保证 TCP、UDP、文件读三条链路的语义正确，不因接口统一而混淆 EOF / 空包 / 真实错误。

**非目标**

1. 不删除 `Bytes` 类型本身；它可以继续作为非热路径工具类型存在。
2. 不改 `AioFile` 的批量提交模型；它不属于本轮 `Bytes` / `span` 兼容层问题。
3. 不改自定义 awaitable 的自由返回类型；本轮只收敛框架内建 I/O 接口。

## 总体方案

### 1. 统一读接口返回值

以下接口统一返回 `std::expected<size_t, IOError>`：

1. `TcpSocket::recv(char*, size_t)`
2. `UdpSocket::recvfrom(char*, size_t, Host*)`
3. `AsyncFile::read(char*, size_t, off_t)`
4. 对应的 `RecvAwaitable` / `RecvFromAwaitable` / `FileReadAwaitable`
5. 对应 `IOHandlers.hpp` 中的 handle 函数

返回约定：

1. `n > 0`：成功读取 `n` 字节
2. `n == 0`：保留底层语义，不额外包装成 `Bytes`
3. `unexpected(IOError)`：真实错误

### 2. 三条链路的具体语义

#### TCP

1. `n > 0` 表示收到数据
2. `n == 0` 表示对端有序关闭
3. `unexpected(IOError)` 表示真实错误

这与当前“返回 `Bytes` 且 `bytes.size() == 0` 视为关闭”的语义一致，只是移除了包装对象。

#### UDP

1. `n > 0` 表示收到一个 datagram
2. `n == 0` 表示合法的空 datagram
3. `unexpected(IOError)` 表示真实错误

这里必须显式保留“0 字节 UDP 包合法”的语义，不能套用 TCP 的 EOF 解释。

#### AsyncFile

1. `n > 0` 表示读取到 `n` 字节
2. `n == 0` 表示 EOF
3. `unexpected(IOError)` 表示真实错误

### 3. 统一 vectored I/O 接口

删除：

1. `readv(std::span<const struct iovec>)`
2. `writev(std::span<const struct iovec>)`
3. `ReadvIOContext` / `WritevIOContext` 内部 owned `std::vector<struct iovec>` fallback

保留：

1. `readv(std::array<struct iovec, N>&, size_t count = N)`
2. `readv(struct iovec (&)[N], size_t count = N)`
3. `writev(std::array<struct iovec, N>&, size_t count = N)`
4. `writev(struct iovec (&)[N], size_t count = N)`
5. 如动态 iovec 数量确有必要，可补一组 borrowed `struct iovec* + size_t count` 接口

设计原则：

1. 热路径只允许 borrowed 元数据
2. `Awaitable` 只保存 borrowed 视图，不再复制 `iovec`
3. 生命周期责任完全交给调用方

### 4. 调用侧迁移方式

原来的用法：

```cpp
auto recvResult = co_await client.recv(buffer, sizeof(buffer));
auto& bytes = recvResult.value();
co_await client.send(bytes.c_str(), bytes.size());
```

迁移后：

```cpp
auto recvResult = co_await client.recv(buffer, sizeof(buffer));
size_t n = recvResult.value();
co_await client.send(buffer, n);
```

当调用方需要文本视图时，显式构造：

```cpp
std::string_view view(buffer, n);
```

这样可以保证：

1. 热路径没有 `Bytes` 包装和拆包
2. 调用方显式区分“原始字节”和“文本语义”

## 删除面

本轮直接删除以下旧接口，不保留兼容转发：

1. `RecvAwaitable` / `RecvFromAwaitable` / `FileReadAwaitable` 的 `std::expected<Bytes, IOError>` 返回
2. `TcpSocket::recv` 的 `Bytes` 风格语义
3. `UdpSocket::recvfrom` 的 `Bytes` 风格语义
4. `AsyncFile::read` 的 `Bytes` 风格语义
5. `readv/writev(std::span<const struct iovec>)`
6. `ReadvIOContext` / `WritevIOContext` 的 owned `vector<iovec>` fallback

保留但降级为非热路径工具的类型：

1. `Bytes`
2. `Send` / `SendTo` / `SendFile` 系列 awaitable
3. `AioFile`

## 影响范围

核心实现文件：

1. `galay-kernel/kernel/Awaitable.h`
2. `galay-kernel/kernel/Awaitable.cc`
3. `galay-kernel/kernel/Awaitable.inl`
4. `galay-kernel/kernel/IOHandlers.hpp`
5. `galay-kernel/async/TcpSocket.h`
6. `galay-kernel/async/UdpSocket.h`
7. `galay-kernel/async/AsyncFile.h`
8. `galay-kernel/async/AsyncFile.cc`

需要同步迁移的调用点：

1. `test/T2-TcpSocket.cc`
2. `test/T4-TcpClient.cc`
3. `test/T5-UdpSocket.cc`
4. `test/T6-UdpServer.cc`
5. `test/T8-FileIo.cc`
6. `test/T10-Timeout.cc`
7. `test/T19-ReadvWritev.cc`
8. `test/T20-RingbufferIo.cc`
9. `test/T22-Sendfile.cc`
10. `test/T23-SendfileBasic.cc`
11. `test/T26-ConcurrentRecvSend.cc`
12. `benchmark/B2-TcpServer.cc`
13. `benchmark/B3-TcpClient.cc`
14. `benchmark/B11-TcpIovServer.cc`
15. `benchmark/B12-TcpIovClient.cc`

## 风险与控制

### 风险 1：UDP 0 字节包语义被误当作 EOF

控制方式：

1. `handleRecvFrom()` 保留 0 字节成功返回
2. UDP 测试补显式断言，确保 0 字节不是错误

### 风险 2：AsyncFile 在 kqueue / io_uring 下语义不一致

控制方式：

1. `FileReadAwaitable` 统一返回 `size_t`
2. `test/T8-FileIo.cc` 在对应平台保持读回校验

### 风险 3：borrowed iovec 生命周期被误用

控制方式：

1. 删除 owned fallback，强制所有调用方显式持有数组生命周期
2. 保留并强化 `T34` / `T35` 这类 borrowed path 测试

### 风险 4：旧示例和 benchmark 仍沿用 `Bytes` 语义

控制方式：

1. 旧接口直接删除，先让编译失败暴露遗漏调用点
2. 所有示例统一改为 `buffer + n` / `std::string_view(buffer, n)`

## 验证要求

最小回归矩阵：

1. TCP：`T2`、`T4`、`T10`、`T19`、`T20`、`T22`、`T23`、`T26`
2. UDP：`T5`、`T6`
3. 文件：`T8`
4. 热路径：`T34`、`T35`、`T36`、`T41`
5. Benchmark：`B2`、`B3`、`B11`、`B12`

平台要求：

1. 本地 `kqueue`：覆盖 TCP / UDP / AsyncFile
2. 远端 `epoll + Release`：覆盖 TCP plain / iov
3. 远端 `io_uring + Release`：覆盖 TCP plain / iov 和自定义 awaitable 基线

## 结论

本设计的核心不是“增加一组更快的接口”，而是把 `galay-kernel` 现有 I/O 主接口彻底收敛到高性能语义上：

1. 读接口统一返回 `size_t`
2. vectored I/O 统一为 borrowed `iovec`
3. 旧的 `Bytes` / `span` 兼容层直接删除

这样可以保证框架内部和对外 API 都只暴露高性能路径，后续性能问题也能更聚焦到 scheduler、waker、syscall 和 backend 本身，而不是被接口层语义噪音掩盖。
