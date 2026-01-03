# 文件 IO API

## AsyncFile

异步文件操作封装。

```cpp
namespace galay::async {

enum class FileOpenMode : int {
    Read      = O_RDONLY,
    Write     = O_WRONLY | O_CREAT,
    ReadWrite = O_RDWR | O_CREAT,
    Append    = O_WRONLY | O_CREAT | O_APPEND,
    Truncate  = O_WRONLY | O_CREAT | O_TRUNC,
};

class AsyncFile {
public:
    // 构造
    AsyncFile(IOScheduler* scheduler);

    // 禁止拷贝，允许移动
    AsyncFile(const AsyncFile&) = delete;
    AsyncFile(AsyncFile&& other) noexcept;

    // 属性
    GHandle handle() const;
    bool isValid() const;

    // 同步操作
    std::expected<void, IOError> open(
        const std::string& path,
        FileOpenMode mode,
        int permissions = 0644
    );
    std::expected<size_t, IOError> size() const;
    std::expected<void, IOError> sync();

    // 异步操作
    FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);
    FileWriteAwaitable write(const char* buffer, size_t length, off_t offset = 0);
    CloseAwaitable close();
};

}
```

## 文件读取示例

```cpp
Coroutine readFile(IOScheduler* scheduler, const std::string& path) {
    AsyncFile file(scheduler);

    auto openResult = file.open(path, FileOpenMode::Read);
    if (!openResult) {
        // 打开失败
        co_return;
    }

    auto sizeResult = file.size();
    if (!sizeResult) {
        co_return;
    }

    size_t fileSize = sizeResult.value();
    std::vector<char> buffer(fileSize);

    auto readResult = co_await file.read(buffer.data(), fileSize, 0);
    if (readResult) {
        auto& bytes = readResult.value();
        // 处理文件内容
    }

    co_await file.close();
}
```

## 文件写入示例

```cpp
Coroutine writeFile(IOScheduler* scheduler, const std::string& path, const std::string& content) {
    AsyncFile file(scheduler);

    auto openResult = file.open(path, FileOpenMode::Write);
    if (!openResult) {
        co_return;
    }

    auto writeResult = co_await file.write(content.data(), content.size(), 0);
    if (writeResult) {
        size_t written = writeResult.value();
        // 写入成功
    }

    file.sync();  // 同步到磁盘
    co_await file.close();
}
```

## 平台差异

| 平台 | 实现方式 | 注意事项 |
|-----|---------|---------|
| macOS (kqueue) | pread/pwrite | 同步调用，通过 kqueue 模拟异步 |
| Linux (epoll) | libaio + eventfd | 需要 O_DIRECT 标志 |
| Linux (io_uring) | io_uring 原生 | 真正的异步 IO |

---

## 性能压测数据

### 文件 IO 性能对比

| 平台 | IO 模型 | IOPS | 吞吐量 | 评级 |
|------|---------|------|--------|------|
| Linux | io_uring | **40,000** | **156.25 MB/s** | ⭐⭐⭐⭐⭐ 卓越 |
| macOS | kqueue | 38,095 | 148.81 MB/s | ⭐⭐⭐⭐⭐ 卓越 |
| Linux | epoll+libaio (批量) | 5,004 | 19.55 MB/s | ⭐⭐⭐⭐ 良好 |
| Linux | epoll+libaio (基准) | 2,663 | 10.40 MB/s | ⭐⭐⭐ 中等 |

**测试配置：**
- Workers: 4
- Operations per worker: 1000
- Block size: 4096 bytes
- Total operations: 8000 (4000 reads + 4000 writes)

### 关键发现

1. 🚀 **io_uring 性能卓越**: 比 epoll+libaio 快 **15倍**
2. 🚀 **kqueue 性能优秀**: 与 io_uring 相当，比 epoll+libaio 快 **14倍**
3. ✅ **批量操作有效**: epoll+libaio 批量模式 (batch=4) 提升 **88%**

### 推荐策略

- 优先使用 io_uring (Linux 5.1+) 或 kqueue (macOS)
- 不支持时使用 epoll+libaio 批量模式
