# 13-运行时Runtime

## 作用

`Runtime` 用于统一管理多个 `IOScheduler` 和 `ComputeScheduler`：

- 自动创建默认数量调度器
- 统一启动/停止
- 轮询负载均衡获取调度器
- 自动启动/停止全局 `TimerScheduler`

## 核心接口

```cpp
class Runtime {
public:
    explicit Runtime(size_t io_count = 0, size_t compute_count = 0);

    bool addIOScheduler(std::unique_ptr<IOScheduler> scheduler);
    bool addComputeScheduler(std::unique_ptr<ComputeScheduler> scheduler);

    void start();
    void stop();
    bool isRunning() const;

    size_t getIOSchedulerCount() const;
    size_t getComputeSchedulerCount() const;

    IOScheduler* getIOScheduler(size_t index);
    ComputeScheduler* getComputeScheduler(size_t index);

    IOScheduler* getNextIOScheduler();
    ComputeScheduler* getNextComputeScheduler();
};
```

## 默认行为

- 当 `io_count=0` 且 `compute_count=0` 时：
- `IO` 调度器数量默认 `2 * CPU核数`
- `Compute` 调度器数量默认 `CPU核数`
- `compute_count=0` 的含义是“自动”，不是“禁用”
- 若需要只启用 IO 调度器，请手动 `addIOScheduler(...)` 后 `start()`

## 用法示例

### 零配置启动

```cpp
Runtime runtime;
runtime.start();

auto* io = runtime.getNextIOScheduler();
auto* compute = runtime.getNextComputeScheduler();
```

### 指定数量

```cpp
Runtime runtime(4, 8); // 4个IO + 8个Compute
runtime.start();
```

### 手动注入调度器

```cpp
Runtime runtime;
runtime.addIOScheduler(std::make_unique<KqueueScheduler>());
runtime.addComputeScheduler(std::make_unique<ComputeScheduler>());
runtime.start();
```

## 典型模式

```cpp
Coroutine ioTask(Runtime* rt) {
    AsyncWaiter<int> waiter;

    auto* compute = rt->getNextComputeScheduler();
    compute->spawn(computeJob(&waiter));

    auto result = co_await waiter.wait();
    (void)result;
}
```
