# 并发工具 API

## MpscChannel

多生产者单消费者通道，用于协程间通信（线程安全）。

```cpp
namespace galay::kernel {

template <typename T>
class MpscChannel {
public:
    using MpscToken = std::thread::id;

    // 构造
    explicit MpscChannel(size_t initialCapacity = 32);

    // 获取当前线程的 token（用于 send 优化）
    static MpscToken getToken();

    // 发送数据（线程安全）
    bool send(T&& value, MpscToken& token);
    bool send(const T& value, MpscToken& token);

    // 批量发送
    bool sendBatch(const std::vector<T>& values, MpscToken& token);
    bool sendBatch(std::vector<T>&& values, MpscToken& token);

    // 异步接收（协程）
    MpscRecvAwaitable<T> recv();
    MpscRecvBatchAwaitable<T> recvBatch(size_t maxCount);

    // 非阻塞接收
    std::optional<T> tryRecv();

    // 状态查询
    size_t size() const;
    bool empty() const;
};

}
```

### 使用示例

```cpp
#include "galay-kernel/concurrency/MpscChannel.h"

MpscChannel<int> channel;

// 生产者（可以在多个线程）
void producer() {
    auto token = MpscChannel<int>::getToken();  // 循环外获取 token
    for (int i = 0; i < 100; ++i) {
        channel.send(i, token);
    }
}

// 消费者协程
Coroutine consumer() {
    while (true) {
        auto value = co_await channel.recv();
        if (value) {
            // 处理数据
        }
    }
}

// 批量接收
Coroutine batchConsumer() {
    auto batch = co_await channel.recvBatch(100);
    if (batch) {
        for (auto& item : *batch) {
            // 处理数据
        }
    }
}
```

### MpscToken 优化

`send()` 方法需要传入 `MpscToken` 参数，这是为了避免每次发送时调用 `std::this_thread::get_id()` 的开销。在循环外获取一次 token，循环内复用：

```cpp
auto token = MpscChannel<int>::getToken();
for (int i = 0; i < count; ++i) {
    channel.send(i, token);  // 复用 token
}
```

### 同线程优化

当生产者和消费者在同一调度器线程时，`send()` 会直接恢复消费者协程，避免队列操作和线程切换。

---

## UnsafeChannel

单调度器内部使用的轻量级异步通道（非线程安全）。

```cpp
namespace galay::kernel {

template <typename T>
class UnsafeChannel {
public:
    // 构造
    UnsafeChannel();

    // 发送数据（仅在调度器线程内调用）
    bool send(T&& value);
    bool send(const T& value);

    // 批量发送
    bool sendBatch(const std::vector<T>& values);
    bool sendBatch(std::vector<T>&& values);

    // 异步接收（协程）
    UnsafeRecvAwaitable<T> recv();
    UnsafeRecvBatchAwaitable<T> recvBatch(size_t maxCount);

    // 非阻塞接收
    std::optional<T> tryRecv();
    std::optional<std::vector<T>> tryRecvBatch(size_t maxCount);

    // 状态查询
    size_t size() const;
    bool empty() const;
};

}
```

### 使用示例

```cpp
#include "galay-kernel/concurrency/UnsafeChannel.h"

UnsafeChannel<int> channel;

// 生产者协程（同一调度器内）
Coroutine producer(UnsafeChannel<int>* ch) {
    for (int i = 0; i < 100; ++i) {
        ch->send(i);  // 无需 token，更简洁
        co_yield true;
    }
}

// 消费者协程（同一调度器内）
Coroutine consumer(UnsafeChannel<int>* ch) {
    while (true) {
        auto value = co_await ch->recv();
        if (value) {
            // 处理数据
        }
    }
}

// 在同一调度器中使用
scheduler.spawn(producer(&channel));
scheduler.spawn(consumer(&channel));
```

### 特点

| 特性 | UnsafeChannel | MpscChannel |
|------|---------------|-------------|
| 线程安全 | ❌ 否 | ✅ 是 |
| 跨调度器 | ❌ 不支持 | ✅ 支持 |
| 性能 | ⚡ 更高 | 良好 |
| send 接口 | 简洁（无 token） | 需要 token |
| 底层实现 | std::queue | ConcurrentQueue |
| 适用场景 | 同调度器内协程通信 | 跨线程/跨调度器通信 |

### 使用警告

⚠️ **重要**：UnsafeChannel 仅供同一调度器内的协程使用：
- 不要在不同调度器的协程间使用
- 不要在普通线程中调用 send（除非该线程就是调度器线程）
- 如需跨调度器通信，请使用 MpscChannel

---

## Bytes

高效字节容器，支持零拷贝。

```cpp
namespace galay::kernel {

class Bytes {
public:
    // 构造
    Bytes();
    Bytes(size_t capacity);

    // 静态工厂
    static Bytes fromCString(const char* data, size_t size, size_t capacity);

    // 属性
    size_t size() const;
    size_t capacity() const;
    bool empty() const;

    // 数据访问
    const char* c_str() const;
    char* data();

    // 操作
    void append(const char* data, size_t size);
    void clear();
    void resize(size_t size);
};

}
```

## GHandle

通用句柄封装。

```cpp
namespace galay::kernel {

struct GHandle {
    int fd = -1;

    static GHandle invalid() { return GHandle{-1}; }
    bool isValid() const { return fd >= 0; }
};

}
```
