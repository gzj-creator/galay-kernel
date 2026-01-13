#include "Buffer.h"
#include <stdexcept>
#include <cassert>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>

namespace galay::kernel
{
    StringMetaData::StringMetaData(std::string &str)
    {
        data = (uint8_t*)str.data();
        size = str.size();
        capacity = str.capacity();
    }

    StringMetaData::StringMetaData(const std::string_view &str)
    {
        data = (uint8_t*)str.data();
        size = str.size();
        capacity = str.length();
    }

    StringMetaData::StringMetaData(const char *str)
    {
        size = strlen(str);
        capacity = size;
        data = (uint8_t*)str;
    }

    StringMetaData::StringMetaData(const uint8_t *str)
    {
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = size;
        data = (uint8_t*)str;
    }

    StringMetaData::StringMetaData(const char* str, size_t length)
    {
        if(length <= 0) throw std::invalid_argument("length must be greater than 0");
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = length;
        size = length;
    }

    StringMetaData::StringMetaData(const uint8_t* str, size_t length)
    {
        if(length <= 0) throw std::invalid_argument("length must be greater than 0");
        size = strlen(reinterpret_cast<const char*>(str));
        capacity = length;
        size = length;
    }

    StringMetaData::StringMetaData(StringMetaData &&other)
        : data(other.data), size(other.size), capacity(other.capacity) 
    {
        other.data = nullptr;
        other.size = 0;
        other.capacity = 0;
    }

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

    StringMetaData::~StringMetaData()
    {
        if(data) {
            data = nullptr;
            size = 0;
            capacity = 0;
        }
    }

    Buffer::Buffer()
    {
    }

    Buffer::Buffer(size_t capacity)
    {
        m_data = mallocString(capacity);
    }

    Buffer::Buffer(const void *data, size_t size)
    {
        m_data = mallocString(size);
        memcpy(m_data.data, data, size);
        m_data.size = size;
    }

    Buffer::Buffer(const std::string &str)
    {
        m_data = mallocString(str.size());
        memcpy(m_data.data, str.data(), str.size());
        m_data.size = str.size();
    }

    void Buffer::clear()
    {
        clearString(m_data);
    }

    char* Buffer::data()
    {
        return reinterpret_cast<char*>(m_data.data);
    }
    
    const char* Buffer::data() const
    {
        return reinterpret_cast<const char*>(m_data.data);
    }

    size_t Buffer::length() const
    {
        return m_data.size;
    }

    size_t Buffer::capacity() const
    {
        return m_data.capacity;
    }

    void Buffer::resize(size_t capacity)
    {
        reallocString(m_data, capacity);
    }

    std::string Buffer::toString() const
    {
        return std::string(reinterpret_cast<const char*>(m_data.data), m_data.size);
    }

    std::string_view Buffer::toStringView() const
    {
        return std::string_view(reinterpret_cast<const char*>(m_data.data), m_data.size);
    }

    Buffer &Buffer::operator=(Buffer &&other)
    {
        if(this != &other) {
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    Buffer::~Buffer()
    {
        clearString(m_data);
    }

    // ============ RingBuffer 实现 ============

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

    RingBuffer::~RingBuffer()
    {
        delete[] m_buffer;
    }

    std::vector<struct iovec> RingBuffer::getWriteIovecs()
    {
        std::vector<struct iovec> iovecs;

        if (m_size == m_capacity) {
            return iovecs;
        }

        if (m_writeIndex >= m_readIndex) {
            // 可写区域: [writeIndex, capacity) 和 [0, readIndex)
            size_t firstChunk = m_capacity - m_writeIndex;
            if (firstChunk > 0) {
                iovecs.push_back({m_buffer + m_writeIndex, firstChunk});
            }
            if (m_readIndex > 0) {
                iovecs.push_back({m_buffer, m_readIndex});
            }
        } else {
            // 可写区域: [writeIndex, readIndex)
            iovecs.push_back({m_buffer + m_writeIndex, m_readIndex - m_writeIndex});
        }

        return iovecs;
    }

    std::vector<struct iovec> RingBuffer::getReadIovecs() const
    {
        std::vector<struct iovec> iovecs;

        if (m_size == 0) {
            return iovecs;
        }

        if (m_readIndex < m_writeIndex) {
            // 可读区域: [readIndex, writeIndex)
            iovecs.push_back({const_cast<char*>(m_buffer + m_readIndex), m_writeIndex - m_readIndex});
        } else {
            // 可读区域: [readIndex, capacity) 和 [0, writeIndex)
            size_t firstChunk = m_capacity - m_readIndex;
            if (firstChunk > 0) {
                iovecs.push_back({const_cast<char*>(m_buffer + m_readIndex), firstChunk});
            }
            if (m_writeIndex > 0) {
                iovecs.push_back({const_cast<char*>(m_buffer), m_writeIndex});
            }
        }

        return iovecs;
    }

    void RingBuffer::produce(size_t len)
    {
        if (len == 0) return;
        size_t actualLen = std::min(len, writable());
        m_writeIndex = (m_writeIndex + actualLen) % m_capacity;
        m_size += actualLen;
    }

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

    void RingBuffer::clear()
    {
        m_readIndex = 0;
        m_writeIndex = 0;
        m_size = 0;
    }

    size_t RingBuffer::write(const void* data, size_t len)
    {
        if (len == 0 || writable() == 0) return 0;

        const char* src = static_cast<const char*>(data);
        size_t toWrite = std::min(len, writable());
        size_t written = 0;

        auto iovecs = getWriteIovecs();
        for (const auto& iov : iovecs) {
            if (written >= toWrite) break;
            size_t chunkSize = std::min(iov.iov_len, toWrite - written);
            std::memcpy(iov.iov_base, src + written, chunkSize);
            written += chunkSize;
        }

        produce(written);
        return written;
    }
}