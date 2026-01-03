# 协程 API

## Coroutine

协程句柄包装类。

```cpp
namespace galay::kernel {

class Coroutine {
public:
    using promise_type = PromiseType;

    // 构造
    Coroutine() noexcept = default;
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept;

    // 拷贝和移动
    Coroutine(const Coroutine& other) noexcept;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other) noexcept;
    Coroutine& operator=(Coroutine&& other) noexcept;

    // 状态查询
    bool isRunning() const;   // 是否正在运行
    bool isSuspend() const;   // 是否已挂起
    bool isDone() const;      // 是否已完成

    // 链式执行
    Coroutine& then(Coroutine co);  // 设置后续协程

    // 等待完成
    WaitResult wait();  // 返回可等待对象
};

}
```

## 定义协程函数

```cpp
Coroutine myCoroutine(IOScheduler* scheduler) {
    // 协程体
    co_await someAwaitable();
    co_return;
}
```

## 链式执行

```cpp
Coroutine first(IOScheduler* scheduler) {
    // 第一个任务
    co_return;
}

Coroutine second(IOScheduler* scheduler) {
    // 第二个任务
    co_return;
}

// first 完成后自动执行 second
scheduler->spawn(first(scheduler).then(second(scheduler)));
```

## 等待其他协程

```cpp
Coroutine outer(IOScheduler* scheduler) {
    Coroutine inner = someCoroutine(scheduler);
    scheduler->spawn(inner);

    co_await inner.wait();  // 等待 inner 完成
    // 继续执行
}
```

## Waker

协程唤醒器，用于在 IO 完成时恢复协程。

```cpp
namespace galay::kernel {

class Waker {
public:
    Waker();
    Waker(std::coroutine_handle<> handle);

    void wakeUp();  // 唤醒关联的协程
};

}
```

## 超时机制

Galay-Kernel 提供了统一的超时机制，支持所有异步 IO 操作。

### timeout() 方法

所有 Awaitable 都支持 `.timeout()` 方法，用于设置操作超时时间。

```cpp
#include "galay-kernel/kernel/Timeout.h"
using namespace std::chrono_literals;

// 带超时的 recv 操作
auto result = co_await socket.recv(buffer, size).timeout(5000ms);

if (!result && IOError::contains(result.error().code(), kTimeout)) {
    // 处理超时
    LogError("Recv timed out");
} else if (result) {
    // 正常处理数据
    auto& bytes = result.value();
}
```

**支持超时的操作：**
- `accept()` - 等待客户端连接
- `connect()` - 连接到服务器
- `recv()` - 接收数据
- `send()` - 发送数据
- `recvFrom()` - UDP 接收
- `sendTo()` - UDP 发送

### sleep() 函数

协程休眠函数，用于暂停协程执行指定时间。

```cpp
#include "galay-kernel/kernel/Timeout.h"
using namespace std::chrono_literals;

Coroutine myCoroutine(IOScheduler* scheduler) {
    // 休眠 1 秒
    co_await galay::kernel::sleep(scheduler, 1000ms);

    // 休眠 500 毫秒
    co_await galay::kernel::sleep(scheduler, 500ms);

    // 零超时立即返回
    co_await galay::kernel::sleep(scheduler, 0ms);
}
```

### 平台实现

| 平台 | 实现方式 |
|------|---------|
| macOS (kqueue) | `EVFILT_TIMER` + `NOTE_USECONDS` |
| Linux (epoll) | `timerfd_create` + `timerfd_settime` |
| Linux (io_uring) | `io_uring_prep_timeout` |

### 示例：带超时的 Echo 服务器

```cpp
Coroutine handleClientWithTimeout(IOScheduler* scheduler, GHandle handle) {
    TcpSocket client(scheduler, handle);
    client.option().handleNonBlock();

    char buffer[4096];
    while (true) {
        // 5 秒超时
        auto recvResult = co_await client.recv(buffer, sizeof(buffer)).timeout(5000ms);

        if (!recvResult) {
            if (IOError::contains(recvResult.error().code(), kTimeout)) {
                LogInfo("Client idle timeout, closing connection");
            } else {
                LogError("Recv error: {}", recvResult.error().message());
            }
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            break;
        }

        // 发送也可以设置超时
        auto sendResult = co_await client.send(bytes.c_str(), bytes.size()).timeout(1000ms);
        if (!sendResult) {
            LogError("Send failed or timed out");
            break;
        }
    }

    co_await client.close();
}
```
