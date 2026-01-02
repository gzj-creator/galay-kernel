# ComputeScheduler 设计文档

## 概述

`ComputeScheduler` 是一个基于线程池的计算任务调度器，专门用于处理 CPU 密集型协程任务。与 `IOScheduler` 不同，它不涉及 IO 事件驱动，纯粹用于协程的并行计算调度。

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                     ComputeScheduler                        │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────┐   │
│  │           ConcurrentQueue<Coroutine>                │   │
│  │                  (无锁队列)                          │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│              ┌────────────┼────────────┐                   │
│              ▼            ▼            ▼                   │
│         ┌────────┐   ┌────────┐   ┌────────┐              │
│         │Worker 1│   │Worker 2│   │Worker N│              │
│         │ Thread │   │ Thread │   │ Thread │              │
│         └────────┘   └────────┘   └────────┘              │
│              │            │            │                   │
│              └────────────┼────────────┘                   │
│                           ▼                                 │
│                  condition_variable                         │
│                    (线程等待/唤醒)                           │
└─────────────────────────────────────────────────────────────┘
```

## 核心组件

### 1. 任务队列
- **类型**: `moodycamel::ConcurrentQueue<Coroutine>`
- **特点**: 无锁多生产者多消费者队列
- **优势**: 高并发下减少锁竞争

### 2. 工作线程池
- **数量**: 可配置，默认为 CPU 核心数
- **模式**: 工作窃取式消费

### 3. 同步机制
- **条件变量**: 用于线程空闲时等待
- **等待计数器**: `m_waiting_threads` 优化唤醒逻辑

## 工作流程

### spawn() - 提交任务
```cpp
void spawn(Coroutine coro) {
    coro.belongScheduler(this);  // 绑定调度器
    m_queue.enqueue(coro);       // 入队（无锁）
    notify();                     // 唤醒等待线程
}
```

### workerLoop() - 工作线程主循环
```
┌─────────────────────────────────────────┐
│              workerLoop                 │
├─────────────────────────────────────────┤
│  while (running) {                      │
│    ┌─────────────────────────────────┐  │
│    │ 1. try_dequeue() 尝试获取任务   │  │
│    │    ├─ 成功 → 执行协程           │  │
│    │    └─ 失败 → 进入等待           │  │
│    └─────────────────────────────────┘  │
│    ┌─────────────────────────────────┐  │
│    │ 2. 等待阶段（队列为空时）        │  │
│    │    ├─ 加锁                      │  │
│    │    ├─ 再次检查队列（避免丢失）   │  │
│    │    ├─ wait_for(100ms)           │  │
│    │    └─ 解锁                      │  │
│    └─────────────────────────────────┘  │
│  }                                      │
│  处理剩余任务后退出                      │
└─────────────────────────────────────────┘
```

## 关键设计决策

### 1. 无锁队列 + 条件变量混合模式

**为什么不纯用无锁队列？**
- 纯无锁需要忙等待（spin），浪费 CPU
- 条件变量允许线程休眠，节省资源

**为什么不纯用互斥锁队列？**
- 高并发下锁竞争严重
- 无锁队列在入队/出队时性能更好

**混合方案**:
- 入队: 无锁 `enqueue()`
- 出队: 先尝试无锁 `try_dequeue()`，失败则等待

### 2. 双重检查避免丢失通知

```cpp
// 第一次检查（无锁）
if (m_queue.try_dequeue(coro)) {
    resume(coro);
    continue;
}

// 加锁后再次检查
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_waiting_threads++;

    // 关键：再次检查，避免在加锁前有任务入队
    if (m_queue.try_dequeue(coro)) {
        m_waiting_threads--;
        lock.unlock();
        resume(coro);
        continue;
    }

    m_cv.wait_for(...);
    m_waiting_threads--;
}
```

### 3. 等待计数器优化唤醒

```cpp
void notify() {
    // 只有当有线程在等待时才唤醒
    if (m_waiting_threads.load() > 0) {
        m_cv.notify_one();
    }
}
```

避免无谓的 `notify_one()` 系统调用。

### 4. 超时等待而非无限等待

```cpp
m_cv.wait_for(lock, 100ms, [...]);
```

- 定期检查 `m_running` 状态
- 避免 `stop()` 时线程无法退出

## 性能数据

### 测试环境
- CPU: 8 核
- 队列: moodycamel::ConcurrentQueue

### 吞吐量
| 任务类型 | 吞吐量 |
|----------|--------|
| 空任务 | 44,189/s |
| 轻量计算 | 41,322/s |
| 重计算 | 303,030/s |

### 延迟
- 平均调度延迟: **17.98 μs**

### 扩展性
| 线程数 | 加速比 |
|--------|--------|
| 1 | 1.00x |
| 2 | 2.67x |
| 4 | 4.00x |

## 优化建议

### 1. 工作窃取（Work Stealing）
**现状**: 所有线程共享一个全局队列
**优化**: 每个线程一个本地队列 + 窃取机制

```
┌─────────┐  ┌─────────┐  ┌─────────┐
│ Local Q │  │ Local Q │  │ Local Q │
│ Thread1 │  │ Thread2 │  │ Thread3 │
└────┬────┘  └────┬────┘  └────┬────┘
     │            │            │
     └─────── 窃取 ◄───────────┘
```

**收益**: 减少全局队列竞争，提高缓存局部性

### 2. 批量出队
**现状**: 每次 `try_dequeue()` 获取一个任务
**优化**: 使用 `try_dequeue_bulk()` 批量获取

```cpp
Coroutine buffer[32];
size_t count = m_queue.try_dequeue_bulk(buffer, 32);
for (size_t i = 0; i < count; ++i) {
    resume(buffer[i]);
}
```

**收益**: 减少队列操作次数，提高吞吐量

### 3. 线程亲和性（Thread Affinity）
**现状**: 线程由 OS 自由调度
**优化**: 绑定线程到特定 CPU 核心

```cpp
#include <pthread.h>
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(thread_id % cpu_count, &cpuset);
pthread_setaffinity_np(thread.native_handle(), sizeof(cpuset), &cpuset);
```

**收益**: 提高缓存命中率，减少上下文切换

### 4. 自适应等待策略
**现状**: 固定 100ms 超时等待
**优化**: 根据负载动态调整

```cpp
// 高负载时：短暂自旋
for (int i = 0; i < 100; ++i) {
    if (m_queue.try_dequeue(coro)) break;
    _mm_pause();  // CPU hint
}

// 低负载时：长时间休眠
m_cv.wait_for(lock, 1000ms, ...);
```

**收益**: 高负载时降低延迟，低负载时节省 CPU

### 5. 优先级队列
**现状**: FIFO 队列，所有任务平等
**优化**: 支持任务优先级

```cpp
struct PriorityCoroutine {
    int priority;
    Coroutine coro;
};
// 使用多个队列或优先级队列
```

**收益**: 关键任务优先执行

### 6. 协程池化
**现状**: 每次 `spawn()` 可能涉及内存分配
**优化**: 复用已完成的协程对象

**收益**: 减少内存分配开销

## 优化优先级建议

| 优化项 | 收益 | 复杂度 | 建议 |
|--------|------|--------|------|
| 批量出队 | 中 | 低 | **推荐** |
| 工作窃取 | 高 | 高 | 大规模时考虑 |
| 自适应等待 | 中 | 中 | 可选 |
| 线程亲和性 | 低-中 | 低 | 可选 |
| 优先级队列 | 中 | 中 | 按需 |
| 协程池化 | 低 | 中 | 暂不需要 |

## 使用示例

```cpp
#include "galay-kernel/kernel/ComputeScheduler.h"

using namespace galay::kernel;

Coroutine computeTask(int id) {
    // CPU 密集型计算
    double result = 0;
    for (int i = 0; i < 1000000; ++i) {
        result += std::sin(i) * std::cos(i);
    }
    std::cout << "Task " << id << " done\n";
    co_return;
}

int main() {
    ComputeScheduler scheduler(4);  // 4 个工作线程
    scheduler.start();

    // 提交多个计算任务
    for (int i = 0; i < 100; ++i) {
        scheduler.spawn(computeTask(i));
    }

    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::seconds(5));

    scheduler.stop();
    return 0;
}
```

## 与 IOScheduler 的对比

| 特性 | ComputeScheduler | IOScheduler |
|------|------------------|-------------|
| 用途 | CPU 密集型任务 | IO 密集型任务 |
| 线程模型 | 多线程池 | 单线程事件循环 |
| 任务队列 | 无锁并发队列 | 无锁并发队列 |
| 等待机制 | 条件变量 | epoll/io_uring |
| 协程挂起 | 不支持 | 支持（等待 IO） |
| 适用场景 | 计算、加密、压缩 | 网络、文件 IO |
