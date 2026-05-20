/**
 * @file buffer.cc
 * @brief StringMetaData 辅助函数、Buffer 和 RingBuffer 的实现
 * @author galay-kernel
 * @version 1.0.0
 */

#include "buffer.h"
#include <stdexcept>
#include <cassert>
#include <sys/uio.h>
#include <unistd.h>

namespace galay::kernel
{
    /**
     * @brief 从 std::string 构造（非拥有视图）
     * @param str 源字符串；调用方必须保证其生命周期
     */
    StringMetaData::StringMetaData(std::string &str)
    {
        data = (uint8_t*)str.data();
        size = str.size();
        capacity = str.capacity();
    }

    /**
     * @brief 从 std::string_view 构造（非拥有视图）
     * @param str 字符串视图；底层数据的生命周期必须超过本对象
     */
    StringMetaData::StringMetaData(const std::string_view &str)
    {
        data = (uint8_t*)str.data();
        size = str.size();
        capacity = str.length();
    }

    /**
     * @brief 从 C 风格字符串构造（非拥有视图）
     * @param str 指向以 null 结尾的字符串的指针
     */
    StringMetaData::StringMetaData(const char *str)
    {
        size = strlen(str);
        capacity = size;
        data = (uint8_t*)str;
    }

    /**
     * @brief 从字节数组构造（非拥有视图）
     * @param str 指向以 null 结尾的字节序列的指针
     */
    StringMetaData::StringMetaData(const uint8_t *str)
    {
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = size;
        data = (uint8_t*)str;
    }

    /**
     * @brief 从 char 指针和显式长度构造（非拥有视图）
     * @param str 数据指针
     * @param length 字节数；必须大于 0
     * @throws std::invalid_argument 若 length 为 0
     */
    StringMetaData::StringMetaData(const char* str, size_t length)
    {
        if(length <= 0) throw std::invalid_argument("length must be greater than 0");
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = length;
        size = length;
    }

    /**
     * @brief 从字节指针和显式长度构造（非拥有视图）
     * @param str 数据指针
     * @param length 字节数；必须大于 0
     * @throws std::invalid_argument 若 length 为 0
     */
    StringMetaData::StringMetaData(const uint8_t* str, size_t length)
    {
        if(length <= 0) throw std::invalid_argument("length must be greater than 0");
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = length;
        size = length;
    }

    /**
     * @brief 移动构造函数；转移数据指针的所有权
     * @param other 源元数据（移后处于归零状态）
     */
    StringMetaData::StringMetaData(StringMetaData &&other)
        : data(other.data), size(other.size), capacity(other.capacity)
    {
        other.data = nullptr;
        other.size = 0;
        other.capacity = 0;
    }

    /**
     * @brief 移动赋值；转移所有权，将 other 置为归零状态
     * @param other 源元数据
     * @return 本对象的引用
     */
    StringMetaData& StringMetaData::operator=(StringMetaData&& other)
    {
        if (this != &other) {
            data = other.data;
            size = other.size;
            capacity = other.capacity;
            other.data = nullptr;
            other.size = 0;
            other.capacity = 0;
        }
        return *this;
    }

    /**
     * @brief 析构函数；重置字段但不释放内存
     * @note 基于所有权的释放由 freeString() 或拥有类处理
     */
    StringMetaData::~StringMetaData()
    {
        if(data) {
            data = nullptr;
            size = 0;
            capacity = 0;
        }
    }

    /**
     * @brief 默认构造空缓冲区，不分配存储空间
     */
    Buffer::Buffer()
    {
    }

    /**
     * @brief 以指定容量构造缓冲区
     * @param capacity 要分配的字节数
     */
    Buffer::Buffer(size_t capacity)
    {
        m_data = mallocString(capacity);
    }

    /**
     * @brief 从原始数据拷贝构造
     * @param data 源指针
     * @param size 要拷贝的字节数
     */
    Buffer::Buffer(const void *data, size_t size)
    {
        m_data = mallocString(size);
        memcpy(m_data.data, data, size);
        m_data.size = size;
    }

    /**
     * @brief 从 std::string 拷贝其内容构造
     * @param str 源字符串
     */
    Buffer::Buffer(const std::string &str)
    {
        m_data = mallocString(str.size());
        memcpy(m_data.data, str.data(), str.size());
        m_data.size = str.size();
    }

    /**
     * @brief 将缓冲区内容清零但不释放内存
     */
    void Buffer::clear()
    {
        clearString(m_data);
    }

    /**
     * @brief 获取缓冲区数据的可变指针
     * @return 指向数据起始位置的 char 指针
     */
    char* Buffer::data()
    {
        return reinterpret_cast<char*>(m_data.data);
    }

    /**
     * @brief 获取缓冲区数据的常量指针
     * @return 指向数据起始位置的 const char 指针
     */
    const char* Buffer::data() const
    {
        return reinterpret_cast<const char*>(m_data.data);
    }

    /**
     * @brief 获取当前存储的字节数
     * @return 数据大小（字节）
     */
    size_t Buffer::length() const
    {
        return m_data.size;
    }

    /**
     * @brief 获取已分配的容量
     * @return 容量（字节）
     */
    size_t Buffer::capacity() const
    {
        return m_data.capacity;
    }

    /**
     * @brief 通过 realloc 调整缓冲区容量
     * @param capacity 新容量（字节）；0 表示释放内存
     */
    void Buffer::resize(size_t capacity)
    {
        reallocString(m_data, capacity);
    }

    /**
     * @brief 将缓冲区内容拷贝为 std::string
     * @return 包含缓冲区数据的新字符串
     */
    std::string Buffer::toString() const
    {
        return std::string(reinterpret_cast<const char*>(m_data.data), m_data.size);
    }

    /**
     * @brief 获取缓冲区数据的零拷贝 string_view
     * @return 引用缓冲区内容的 string_view
     */
    std::string_view Buffer::toStringView() const
    {
        return std::string_view(reinterpret_cast<const char*>(m_data.data), m_data.size);
    }

    /**
     * @brief 移动赋值；释放当前存储并接管 other 的数据
     * @param other 源缓冲区（移后为空）
     * @return 本对象的引用
     */
    Buffer &Buffer::operator=(Buffer &&other)
    {
        if(this != &other) {
            freeString(m_data);
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    /**
     * @brief 析构函数；将内部存储清零并释放
     */
    Buffer::~Buffer()
    {
        clearString(m_data);
    }

    // ============ RingBuffer 实现 ============

    /**
     * @brief 以指定固定容量构造环形缓冲区
     * @param capacity 要分配的字节数；必须大于 0
     * @throws std::invalid_argument 若 capacity 为 0
     */
    RingBuffer::RingBuffer(size_t capacity)
        : m_buffer(new char[capacity])
        , m_capacity(capacity)
        , m_readIndex(0)
        , m_writeIndex(0)
        , m_size(0)
    {
        if (capacity == 0) {
            throw std::invalid_argument("RingBuffer capacity must be greater than 0");
        }
    }

    /**
     * @brief 移动构造函数；接管 other 的缓冲区
     * @param other 源环形缓冲区（移后为空）
     */
    RingBuffer::RingBuffer(RingBuffer&& other) noexcept
        : m_buffer(other.m_buffer)
        , m_capacity(other.m_capacity)
        , m_readIndex(other.m_readIndex)
        , m_writeIndex(other.m_writeIndex)
        , m_size(other.m_size)
    {
        other.m_buffer = nullptr;
        other.m_capacity = 0;
        other.m_readIndex = 0;
        other.m_writeIndex = 0;
        other.m_size = 0;
    }

    /**
     * @brief 移动赋值；释放当前缓冲区并接管 other 的
     * @param other 源环形缓冲区（移后为空）
     * @return 本对象的引用
     */
    RingBuffer& RingBuffer::operator=(RingBuffer&& other) noexcept
    {
        if (this != &other) {
            delete[] m_buffer;
            m_buffer = other.m_buffer;
            m_capacity = other.m_capacity;
            m_readIndex = other.m_readIndex;
            m_writeIndex = other.m_writeIndex;
            m_size = other.m_size;

            other.m_buffer = nullptr;
            other.m_capacity = 0;
            other.m_readIndex = 0;
            other.m_writeIndex = 0;
            other.m_size = 0;
        }
        return *this;
    }

    /**
     * @brief 析构函数；释放底层缓冲区
     */
    RingBuffer::~RingBuffer()
    {
        delete[] m_buffer;
    }

    /**
     * @brief 填充描述可写区域的 iovec 数组
     * @param out 要填充的 iovec 结构体数组
     * @param max_iovecs 最大 iovec 条目数
     * @return 填充的 iovec 条目数（0、1 或 2）
     *
     * @details 返回最多两个 iovec：一个从写索引到缓冲区末尾，
     * 可选的第二个环绕到起始位置。
     */
    size_t RingBuffer::getWriteIovecs(struct iovec* out, size_t max_iovecs) const
    {
        if (out == nullptr || max_iovecs == 0 || m_size == m_capacity) {
            return 0;
        }

        size_t count = 0;
        if (m_writeIndex >= m_readIndex) {
            // 可写区域: [writeIndex, capacity) 和 [0, readIndex)
            const size_t firstChunk = m_capacity - m_writeIndex;
            if (firstChunk > 0 && count < max_iovecs) {
                out[count++] = {m_buffer + m_writeIndex, firstChunk};
            }
            if (m_readIndex > 0 && count < max_iovecs) {
                out[count++] = {m_buffer, m_readIndex};
            }
        } else {
            // 可写区域: [writeIndex, readIndex)
            if (count < max_iovecs) {
                out[count++] = {m_buffer + m_writeIndex, m_readIndex - m_writeIndex};
            }
        }
        return count;
    }

    /**
     * @brief 填充描述可读区域的 iovec 数组
     * @param out 要填充的 iovec 结构体数组
     * @param max_iovecs 最大 iovec 条目数
     * @return 填充的 iovec 条目数（0、1 或 2）
     *
     * @details 返回最多两个 iovec：一个从读索引向前，
     * 可选的第二个用于环绕到起始位置的数据。
     */
    size_t RingBuffer::getReadIovecs(struct iovec* out, size_t max_iovecs) const
    {
        if (out == nullptr || max_iovecs == 0 || m_size == 0) {
            return 0;
        }

        size_t count = 0;
        if (m_readIndex < m_writeIndex) {
            // 可读区域: [readIndex, writeIndex)
            if (count < max_iovecs) {
                out[count++] = {
                    const_cast<char*>(m_buffer + m_readIndex),
                    m_writeIndex - m_readIndex
                };
            }
        } else {
            // 可读区域: [readIndex, capacity) 和 [0, writeIndex)
            const size_t firstChunk = m_capacity - m_readIndex;
            if (firstChunk > 0 && count < max_iovecs) {
                out[count++] = {const_cast<char*>(m_buffer + m_readIndex), firstChunk};
            }
            if (m_writeIndex > 0 && count < max_iovecs) {
                out[count++] = {const_cast<char*>(m_buffer), m_writeIndex};
            }
        }
        return count;
    }

    /**
     * @brief 数据写入后推进写指针
     * @param len 已写入的字节数；截断至 writable()
     */
    void RingBuffer::produce(size_t len)
    {
        if (len == 0) return;
        size_t actualLen = std::min(len, writable());
        m_writeIndex = (m_writeIndex + actualLen) % m_capacity;
        m_size += actualLen;
    }

    /**
     * @brief 推进读指针，丢弃已消费的数据
     * @param len 要消费的字节数；截断至 readable()
     *
     * @details 当缓冲区变空时重置两个指针为 0。
     */
    void RingBuffer::consume(size_t len)
    {
        if (len == 0) return;
        size_t actualLen = std::min(len, m_size);
        m_readIndex = (m_readIndex + actualLen) % m_capacity;
        m_size -= actualLen;

        if (m_size == 0) {
            m_readIndex = 0;
            m_writeIndex = 0;
        }
    }

    /**
     * @brief 将环形缓冲区重置为空状态，不释放内存
     */
    void RingBuffer::clear()
    {
        m_readIndex = 0;
        m_writeIndex = 0;
        m_size = 0;
    }

    /**
     * @brief 使用 scatter-gather iovecs 将数据拷贝到环形缓冲区
     * @param data 源指针
     * @param len 要写入的字节数
     * @return 实际写入的字节数（缓冲区满时可能少于请求量）
     */
    size_t RingBuffer::write(const void* data, size_t len)
    {
        if (len == 0 || writable() == 0) return 0;

        const char* src = static_cast<const char*>(data);
        size_t toWrite = std::min(len, writable());
        size_t written = 0;

        std::array<struct iovec, 2> iovecs{};
        const size_t iovecCount = getWriteIovecs(iovecs);
        for (size_t i = 0; i < iovecCount; ++i) {
            const auto& iov = iovecs[i];
            if (written >= toWrite) break;
            size_t chunkSize = std::min(iov.iov_len, toWrite - written);
            std::memcpy(iov.iov_base, src + written, chunkSize);
            written += chunkSize;
        }

        produce(written);
        return written;
    }
}
