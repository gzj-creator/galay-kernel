# Galay-Kernel Benchmark Report

## 测试环境

- **平台**: macOS (Darwin 24.6.0)
- **架构**: ARM64 (Apple Silicon)
- **IO模型**: kqueue
- **调度器**: 单线程协程调度器
- **测试日期**: 2026-01-01

## 测试配置

- **消息大小**: 256 bytes
- **测试时长**: 5 seconds per test
- **测试模式**: Echo (客户端发送 -> 服务器回显 -> 客户端接收)

## 测试结果

### 1. 不同并发连接数对比

| 并发连接数 | 平均 QPS | 平均吞吐量 | 总请求数 | 错误数 | 成功率 |
|-----------|---------|-----------|---------|-------|-------|
| 100 | 279,569 | 136.5 MB/s | 1,397,848 | 0 | 100% |
| 500 | 275,722 | 134.6 MB/s | 1,378,614 | 0 | 100% |
| 1000 | 263,878 | 128.8 MB/s | 1,319,394 | 0 | 100% |

### 2. 详细测试数据

#### 100 并发连接

```
=== Benchmark Client ===
Target: 127.0.0.1:8080
Connections: 100
Message Size: 256 bytes
Duration: 5 seconds

[1s] QPS: 304,714 | Throughput: 148.8 MB/s
[2s] QPS: 266,679 | Throughput: 130.2 MB/s
[3s] QPS: 281,190 | Throughput: 137.3 MB/s
[4s] QPS: 269,299 | Throughput: 131.5 MB/s
[5s] QPS: 270,609 | Throughput: 132.1 MB/s

=== Final Results ===
Total Requests: 1,397,848
Average QPS: 279,569
Average Throughput: 136.5 MB/s
```

#### 500 并发连接

```
=== Benchmark Client ===
Target: 127.0.0.1:8080
Connections: 500
Message Size: 256 bytes
Duration: 5 seconds

[1s] QPS: 266,582 | Throughput: 130.2 MB/s
[2s] QPS: 280,373 | Throughput: 136.9 MB/s
[3s] QPS: 265,223 | Throughput: 129.5 MB/s
[4s] QPS: 281,389 | Throughput: 137.4 MB/s
[5s] QPS: 279,291 | Throughput: 136.4 MB/s

=== Final Results ===
Total Requests: 1,378,614
Average QPS: 275,722
Average Throughput: 134.6 MB/s
```

#### 1000 并发连接

```
=== Benchmark Client ===
Target: 127.0.0.1:8080
Connections: 1000
Message Size: 256 bytes
Duration: 5 seconds

[1s] QPS: 252,277 | Throughput: 123.2 MB/s
[2s] QPS: 258,035 | Throughput: 126.0 MB/s
[3s] QPS: 275,238 | Throughput: 134.4 MB/s
[4s] QPS: 264,240 | Throughput: 129.0 MB/s
[5s] QPS: 264,666 | Throughput: 129.2 MB/s

=== Final Results ===
Total Requests: 1,319,394
Average QPS: 263,878
Average Throughput: 128.8 MB/s
```

## 性能分析

### 1. 扩展性

从 100 到 1000 并发连接，QPS 仅下降约 **5.6%**：
- 100 连接: 279,569 QPS
- 1000 连接: 263,878 QPS
- 下降幅度: (279,569 - 263,878) / 279,569 = 5.6%

这表明调度器在高并发场景下具有良好的扩展性。

### 2. 稳定性

- 所有测试的错误率均为 **0%**
- QPS 波动范围在 ±10% 以内
- 无连接失败或请求超时

### 3. 吞吐量

- 稳定在 **128-137 MB/s** 范围内
- 峰值吞吐量达到 **148.8 MB/s**

### 4. 总处理能力

整个测试期间服务器共处理：
- **总连接数**: 1,600
- **总请求数**: 4,095,856
- **总数据量**: ~2 GB

## 架构特点

1. **单线程设计**: 使用单线程 kqueue 事件循环，避免锁竞争
2. **协程调度**: 基于 C++20 协程实现异步 IO
3. **零拷贝**: 使用 `Bytes` 类实现高效数据传输
4. **无锁队列**: 使用 `moodycamel::ConcurrentQueue` 进行协程调度

## 使用方法

### 启动服务器

```bash
./bin/bench_server [port]
# 例如: ./bin/bench_server 8080
```

### 启动客户端

```bash
./bin/bench_client [options]

选项:
  -h <host>        服务器地址 (默认: 127.0.0.1)
  -p <port>        服务器端口 (默认: 8080)
  -c <connections> 并发连接数 (默认: 100)
  -s <size>        消息大小(字节) (默认: 256)
  -d <duration>    测试时长(秒) (默认: 10)

# 例如: ./bin/bench_client -c 1000 -s 512 -d 30
```

## 结论

Galay-Kernel 的单线程协程调度器在高并发场景下表现优秀：

- **高性能**: 单线程达到 **26-28万 QPS**
- **高稳定性**: 零错误率，QPS 波动小
- **良好扩展性**: 1000 并发连接仅 5.6% 性能下降
- **高吞吐**: 稳定 **130+ MB/s** 数据传输
