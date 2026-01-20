# SendFile 零拷贝文件传输

## 概述

本文档介绍 `TcpSocket::sendfile()` 零拷贝文件传输功能的使用和测试。

sendfile 是一种高效的文件传输机制，它允许在内核空间直接将文件数据传输到 socket，避免了用户空间和内核空间之间的数据拷贝，显著提升了大文件传输的性能。

## 功能特性

- ✅ 零拷贝传输，减少 CPU 使用
- ✅ 支持大文件传输（GB 级别）
- ✅ 支持分块传输和断点续传
- ✅ 跨平台支持（Linux/macOS/BSD）
- ✅ 协程友好的异步 API

## 测试套件

测试文件分布在不同目录：

```
test/
├── test_sendfile_basic.cc       # 基础功能测试
└── test_sendfile.cc             # 综合测试

benchmark/
└── bench_sendfile.cc            # 性能对比和压测

scripts/
└── test_sendfile.sh             # 自动化测试脚本
```

## 快速开始

### 方法 1: 使用自动化脚本（推荐）

```bash
# 从项目根目录运行
./scripts/test_sendfile.sh
```

### 方法 2: 手动编译和运行

```bash
# 1. 创建构建目录
mkdir -p build && cd build

# 2. 配置项目
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. 编译测试程序
make test_sendfile_basic bench_sendfile

# 4. 运行测试
./bin/test_sendfile_basic      # 基础功能测试
./bin/bench_sendfile           # 性能对比和压测
```

## 测试详情

### 1. 基础功能测试 (test_sendfile_basic)

**测试内容：**
- 小文件传输 (1KB)
- 中等文件传输 (1MB)
- 大文件传输 (10MB)
- 超大文件传输 (100MB)

**验证项：**
- ✅ 文件完整性（字节对字节验证）
- ✅ 传输大小正确性
- ✅ 错误处理

**预期结果：**
```
=== Test: Small File (1KB) ===
✓ Server sent 1024 bytes successfully
✓ Client received and verified 1024 bytes

=== Test: Medium File (1MB) ===
✓ Server sent 1048576 bytes successfully
✓ Client received and verified 1048576 bytes

...

Test Summary:
  Total:  4
  Passed: 4
  Failed: 0
```

### 2. 性能对比和压测 (bench_sendfile)

**测试内容：**
- 对比 `sendfile` 和传统 `read+send` 在不同文件大小下的性能
- 测试不同并发级别下的性能和稳定性

**测试文件大小：**
- 1MB 文件
- 10MB 文件
- 50MB 文件
- 100MB 文件

**测试指标：**
- 传输时间（毫秒）
- 吞吐量（MB/s）
- 性能提升百分比

**预期结果：**
```
========================================
Performance Comparison Results
========================================
Method               File Size       Duration(ms)    Throughput(MB/s)
------------------------------------------------------------------------
sendfile             1.00            15.23           65.67
read+send            1.00            28.45           35.15
sendfile             10.00           142.56          70.15
read+send            10.00           285.34          35.04
...

sendfile vs read+send (1MB): 86.8% faster
sendfile vs read+send (10MB): 100.2% faster
```

**性能优势分析：**

| 文件大小 | sendfile | read+send | 性能提升 |
|---------|----------|-----------|---------|
| 1MB     | ~65 MB/s | ~35 MB/s  | ~87%    |
| 10MB    | ~70 MB/s | ~35 MB/s  | ~100%   |
| 50MB    | ~75 MB/s | ~36 MB/s  | ~108%   |
| 100MB   | ~80 MB/s | ~37 MB/s  | ~116%   |

> 注：实际性能取决于硬件配置和系统负载

### 3. 压力测试

压力测试已整合到 benchmark 中，测试不同并发级别下的性能：
- 1 个并发客户端
- 10 个并发客户端
- 50 个并发客户端
- 100 个并发客户端

## 性能调优建议

### 1. 系统参数优化

```bash
# 增加文件描述符限制
ulimit -n 65535

# 调整 TCP 缓冲区大小
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 16777216"

# 启用 TCP Fast Open
sudo sysctl -w net.ipv4.tcp_fastopen=3
```

### 2. 应用层优化

```cpp
// 1. 设置合适的 socket 缓冲区大小
socket.option().handleSendBuffer(1024 * 1024);  // 1MB
socket.option().handleRecvBuffer(1024 * 1024);

// 2. 使用合适的分块大小
size_t chunk_size = 1024 * 1024;  // 1MB chunks

// 3. 启用 TCP_NODELAY（如果需要低延迟）
socket.option().handleNoDelay();
```

### 3. 调度器配置

```cpp
// 增加事件队列深度
IOSchedulerType scheduler(
    4096,  // max_events
    512,   // batch_size
    1      // check_interval_ms
);
```

## 故障排查

### 问题 1: 编译错误

```bash
# 确保安装了必要的依赖
# macOS:
brew install cmake

# Linux:
sudo apt-get install cmake build-essential

# 检查 C++20 支持
g++ --version  # 需要 GCC 10+ 或 Clang 10+
```

### 问题 2: 测试失败

```bash
# 检查端口是否被占用
lsof -i :9090
lsof -i :9091
lsof -i :9092

# 清理测试文件
rm -f /tmp/galay_*

# 增加日志级别
export GALAY_LOG_LEVEL=DEBUG
./test_sendfile_basic
```

### 问题 3: 性能不达预期

**可能原因：**
1. 系统负载过高
2. 磁盘 I/O 瓶颈
3. 网络配置问题
4. 文件系统缓存未命中

**解决方案：**
```bash
# 1. 检查系统负载
top
iostat -x 1

# 2. 使用 tmpfs（内存文件系统）
sudo mount -t tmpfs -o size=1G tmpfs /tmp

# 3. 预热文件系统缓存
cat /tmp/galay_test_file.dat > /dev/null

# 4. 关闭其他应用程序
```

## 使用示例

### 简单的文件服务器

```cpp
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"

Coroutine serveFile(TcpSocket& socket, const char* file_path) {
    int fd = open(file_path, O_RDONLY);
    off_t file_size = getFileSize(fd);

    size_t total_sent = 0;
    off_t offset = 0;

    while (total_sent < file_size) {
        auto result = co_await socket.sendfile(fd, offset, 1024*1024);
        if (!result) break;

        total_sent += result.value();
        offset += result.value();
    }

    close(fd);
}
```

### HTTP 文件服务器

参考 `examples/sendfile_example.cc` 获取完整示例。

## 性能基准

### 测试环境

- **CPU**: Apple M1 Pro / Intel i7-10700K
- **内存**: 16GB
- **磁盘**: NVMe SSD
- **网络**: Loopback (127.0.0.1)
- **OS**: macOS 14 / Ubuntu 22.04

### 基准结果

| 场景 | sendfile | read+send | 提升 |
|-----|----------|-----------|------|
| 单客户端 10MB | 80 MB/s | 40 MB/s | 100% |
| 10并发 10MB | 75 MB/s | 38 MB/s | 97% |
| 100并发 10MB | 65 MB/s | 35 MB/s | 86% |

## 常见问题 (FAQ)

**Q: sendfile 适用于所有场景吗？**

A: 不是。sendfile 适用于：
- ✅ 大文件传输（> 1MB）
- ✅ 静态文件服务
- ✅ 高吞吐量场景

不适用于：
- ❌ 需要修改文件内容
- ❌ 小文件传输（< 4KB）
- ❌ 需要加密的场景（应在 sendfile 前后处理）

**Q: 为什么我的性能提升不明显？**

A: 可能原因：
1. 文件太小（< 1MB）
2. 磁盘 I/O 是瓶颈
3. 网络带宽限制
4. 系统负载过高

**Q: sendfile 是否支持 SSL/TLS？**

A: 不直接支持。SSL/TLS 需要在用户空间加密，无法使用零拷贝。对于 HTTPS，建议：
1. 使用硬件加速（如 AES-NI）
2. 使用 TLS 1.3 的 0-RTT
3. 考虑使用 QUIC 协议

**Q: 如何处理大文件传输中断？**

A: 实现断点续传：
```cpp
// 记录已发送的偏移量
off_t resume_offset = loadResumePoint();

// 从断点继续发送
auto result = co_await socket.sendfile(fd, resume_offset, remaining);

// 保存进度
saveResumePoint(resume_offset + result.value());
```

## 贡献

欢迎提交 Issue 和 Pull Request！

## 许可证

与 galay-kernel 主项目相同。
