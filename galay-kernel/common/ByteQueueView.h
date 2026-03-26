#ifndef GALAY_KERNEL_BYTE_QUEUE_VIEW_H
#define GALAY_KERNEL_BYTE_QUEUE_VIEW_H

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace galay::kernel {

/**
 * @brief 面向解析器的只增字节队列视图
 * @details 以单个连续 `std::vector<char>` 存储数据，支持追加、查看和消费。
 */
class ByteQueueView {
public:
    ByteQueueView() = default;  ///< 构造空字节队列视图

    explicit ByteQueueView(size_t reserve_size) {
        reserve(reserve_size);
    }

    void reserve(size_t capacity) {
        m_storage.reserve(capacity);
    }

    void append(const char* data, size_t length) {
        if (length == 0) {
            return;
        }
        if (m_read_offset == m_storage.size()) {
            clear();
        }
        m_storage.insert(m_storage.end(), data, data + length);
    }

    void append(std::string_view bytes) {
        append(bytes.data(), bytes.size());
    }

    [[nodiscard]] size_t size() const noexcept {
        return m_storage.size() - m_read_offset;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] bool has(size_t length) const noexcept {
        return size() >= length;
    }

    [[nodiscard]] const char* data() const noexcept {
        return m_storage.data() + m_read_offset;
    }

    [[nodiscard]] std::string_view view(size_t offset, size_t length) const noexcept {
        if (offset + length > size()) {
            return {};
        }
        return std::string_view(data() + offset, length);
    }

    void consume(size_t length) {
        if (length >= size()) {
            clear();
            return;
        }
        m_read_offset += length;
        compactIfNeeded();
    }

    void clear() noexcept {
        m_storage.clear();
        m_read_offset = 0;
    }

private:
    void compactIfNeeded() {
        const size_t readable = size();
        if (m_read_offset == 0) {
            return;
        }
        if (readable == 0) {
            clear();
            return;
        }
        if (m_read_offset < 4096 && m_read_offset * 2 < m_storage.size()) {
            return;
        }
        std::memmove(m_storage.data(), m_storage.data() + m_read_offset, readable);
        m_storage.resize(readable);
        m_read_offset = 0;
    }

    std::vector<char> m_storage;
    size_t m_read_offset = 0;
};

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_BYTE_QUEUE_VIEW_H
