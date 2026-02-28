# 运行时Runtime

## 作用

`Runtime` 用于统一管理多个 `IOScheduler` 和 `ComputeScheduler`：

- 自动创建默认数量调度器
- 统一启动/停止
- 轮询负载均衡获取调度器
- 自动启动/停止全局 `TimerScheduler`
- 支持 CPU 绑核配置（Sequential / Custom 两种模式）

## 核心接口

```cpp
class Runtime {
public:
    explicit Runtime(const RuntimeConfig& config = RuntimeConfig{});

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

## RuntimeBuilder

推荐通过 `RuntimeBuilder` 构建 `Runtime`：

```cpp
class RuntimeBuilder {
public:
    RuntimeBuilder& ioSchedulerCount(size_t n);
    RuntimeBuilder& computeSchedulerCount(size_t n);
    RuntimeBuilder& sequentialAffinity(size_t io_count, size_t compute_count);
    bool customAffinity(std::vector<uint32_t> io_cpus, std::vector<uint32_t> compute_cpus);
    RuntimeBuilder& applyAffinity(const RuntimeAffinityConfig& aff);
    Runtime build() const;
};
```

## 默认行为

- 当 `io_scheduler_count=0` 且 `compute_scheduler_count=0` 时：
- `IO` 调度器数量默认 `2 * CPU核数`
- `Compute` 调度器数量默认 `CPU核数`
- `compute_scheduler_count=0` 的含义是"自动"，不是"禁用"
- 若需要只启用 IO 调度器，请手动 `addIOScheduler(...)` 后 `start()`

## 用法示例

### 零配置启动

```cpp
Runtime runtime = RuntimeBuilder().build();
runtime.start();

auto* io = runtime.getNextIOScheduler();
auto* compute = runtime.getNextComputeScheduler();
```

### 指定数量

```cpp
Runtime runtime = RuntimeBuilder()
    .ioSchedulerCount(4)
    .computeSchedulerCount(8)
    .build();
runtime.start();
```

### 顺序绑核

从 CPU 0 开始依次分配，超出 CPU 数量后回绕：

```cpp
Runtime runtime = RuntimeBuilder()
    .ioSchedulerCount(4)
    .computeSchedulerCount(2)
    .sequentialAffinity(4, 2)  // 4个IO + 2个Compute 依次绑核
    .build();
runtime.start();
```

### 自定义绑核

向量长度必须与调度器数量严格一致，否则返回 false 且不修改配置：

```cpp
auto builder = RuntimeBuilder()
    .ioSchedulerCount(2)
    .computeSchedulerCount(2);
bool ok = builder.customAffinity({0, 1}, {2, 3});  // IO绑CPU0/1, Compute绑CPU2/3
Runtime runtime = builder.build();
runtime.start();
```

### 手动注入调度器

```cpp
Runtime runtime = RuntimeBuilder().build();
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
