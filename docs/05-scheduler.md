# 调度器 API

## IOScheduler (抽象基类)

所有平台调度器的基类接口。

```cpp
namespace galay::kernel {

class IOScheduler : public Scheduler {
public:
    // 注册 IO 事件
    virtual int addAccept(IOController* controller) = 0;
    virtual int addConnect(IOController* controller) = 0;
    virtual int addRecv(IOController* controller) = 0;
    virtual int addSend(IOController* controller) = 0;
    virtual int addClose(int fd) = 0;
    virtual int addFileRead(IOController* controller) = 0;
    virtual int addFileWrite(IOController* controller) = 0;

    // 提交协程
    virtual void spawn(Coroutine coro) = 0;
};

}
```

## KqueueScheduler (macOS)

基于 kqueue 的调度器实现。

```cpp
namespace galay::kernel {

class KqueueScheduler : public IOScheduler {
public:
    // 构造函数
    KqueueScheduler(
        int max_events = 1024,           // 单次处理最大事件数
        int batch_size = 256,            // 协程批处理大小
        int check_interval_ms = 1        // 事件检查间隔 (毫秒)
    );

    // 生命周期
    void start();   // 启动调度器线程
    void stop();    // 停止调度器
    void notify();  // 唤醒调度器

    // 提交协程
    void spawn(Coroutine coro) override;
};

}
```

**使用示例：**

```cpp
#include "galay-kernel/kernel/KqueueScheduler.h"

int main() {
    KqueueScheduler scheduler;
    scheduler.start();

    scheduler.spawn(myCoroutine(&scheduler));

    // 等待信号或其他退出条件
    std::this_thread::sleep_for(std::chrono::seconds(60));

    scheduler.stop();
    return 0;
}
```

## EpollScheduler (Linux)

基于 epoll + libaio 的调度器实现。

```cpp
namespace galay::kernel {

class EpollScheduler : public IOScheduler {
public:
    EpollScheduler(
        int max_events = 1024,
        int batch_size = 256,
        int check_interval_ms = 1
    );

    void start();
    void stop();
    void notify();
    void spawn(Coroutine coro) override;
};

}
```

## IOUringScheduler (Linux 5.1+)

基于 io_uring 的高性能调度器实现。

```cpp
namespace galay::kernel {

class IOUringScheduler : public IOScheduler {
public:
    IOUringScheduler(
        int queue_depth = 4096,   // io_uring 队列深度
        int batch_size = 256      // 协程批处理大小
    );

    void start();
    void stop();
    void notify();
    void spawn(Coroutine coro) override;
};

}
```

**io_uring 特性：**
- 原生支持网络和文件 IO
- 零系统调用开销 (SQPOLL 模式)
- 批量提交和完成

## 编译选项

### 宏定义

| 宏 | 默认值 | 说明 |
|---|-------|------|
| `USE_KQUEUE` | macOS 自动定义 | 使用 kqueue 调度器 |
| `USE_EPOLL` | Linux 自动定义 | 使用 epoll 调度器 |
| `USE_IOURING` | 手动定义 | 使用 io_uring 调度器 |
| `GALAY_SCHEDULER_MAX_EVENTS` | 1024 | 单次处理最大事件数 |
| `GALAY_SCHEDULER_BATCH_SIZE` | 256 | 协程批处理大小 |
| `GALAY_SCHEDULER_CHECK_INTERVAL_MS` | 1 | 事件检查间隔 (毫秒) |
| `GALAY_SCHEDULER_QUEUE_DEPTH` | 4096 | io_uring 队列深度 |

### CMake 选项

```cmake
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(ENABLE_LOG "Enable spdlog logging" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)
```
