/**
 * @file queue_view.h
 * @brief 用于协议解析的仅追加字节队列视图
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details ByteQueueView 在单个连续的 std::vector<char> 中存储数据，
 * 并跟踪读偏移量以避免频繁重新分配。数据在尾部追加，
 * 通过推进偏移量来消费；当已消费区域超过阈值时，
 * 存储会在原地压缩。专为从网络流中增量解析字节而设计。
 */

#ifndef GALAY_KERNEL_BYTE_QUEUE_VIEW_H
#define GALAY_KERNEL_BYTE_QUEUE_VIEW_H

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace galay::kernel {

/**
 * @brief 用于协议解析的仅追加字节队列视图
 *
 * @details 在单个连续的 std::vector<char> 中存储数据并跟踪读偏移量
 * 以避免频繁重新分配。数据在尾部追加，通过推进偏移量来消费；
 * 当已消费区域超过阈值时，存储会在原地压缩。
 */
class ByteQueueView {
public:
    /**
     * @brief 默认构造空队列
     */
    ByteQueueView() = default;

    /**
     * @brief 以预分配容量构造
     * @param reserve_size 要预留的字节数
     */
    explicit ByteQueueView(size_t reserve_size) {
        reserve(reserve_size);
    }

    /**
     * @brief 预留存储容量
     * @param capacity 最少要预留的字节数
     */
    void reserve(size_t capacity) {
        m_storage.reserve(capacity);
    }

    /**
     * @brief 在队列尾部追加原始字节
     * @param data 指向要追加的字节的指针
     * @param length 字节数
     *
     * @details 若之前追加的数据已全部消费，则先清空内部存储以避免无限增长。
     */
    void append(const char* data, size_t length) {
        if (length == 0) {
            return;
        }
        if (m_read_offset == m_storage.size()) {
            clear();
        }
        m_storage.insert(m_storage.end(), data, data + length);
    }

    /**
     * @brief 在队列尾部追加 string_view
     * @param bytes 要追加的数据
     */
    void append(std::string_view bytes) {
        append(bytes.data(), bytes.size());
    }

    /**
     * @brief 获取可读字节数
     * @return 可读字节数
     */
    [[nodiscard]] size_t size() const noexcept {
        return m_storage.size() - m_read_offset;
    }

    /**
     * @brief 检查队列是否没有可读数据
     * @return 若为空则返回 true
     */
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /**
     * @brief 检查是否至少有 `length` 个字节可读
     * @param length 所需字节数
     * @return 若可读字节 >= length 则返回 true
     */
    [[nodiscard]] bool has(size_t length) const noexcept {
        return size() >= length;
    }

    /**
     * @brief 获取第一个可读字节的指针
     * @return 指向读位置的常量指针
     */
    [[nodiscard]] const char* data() const noexcept {
        return m_storage.data() + m_read_offset;
    }

    /**
     * @brief 获取可读数据子范围的 string_view
     * @param offset 从当前读位置开始的字节偏移
     * @param length 视图中的字节数
     * @return string_view，若范围超出可读数据则返回空 string_view
     */
    [[nodiscard]] std::string_view view(size_t offset, size_t length) const noexcept {
        if (offset + length > size()) {
            return {};
        }
        return std::string_view(data() + offset, length);
    }

    /**
     * @brief 推进读位置，丢弃已消费的字节
     * @param length 要消费的字节数
     *
     * @details 若已消费区域足够大，则通过 memmove 压缩内部存储以回收空间。
     */
    void consume(size_t length) {
        if (length >= size()) {
            clear();
            return;
        }
        m_read_offset += length;
        compactIfNeeded();
    }

    /**
     * @brief 丢弃所有数据并重置读偏移
     */
    void clear() noexcept {
        m_storage.clear();
        m_read_offset = 0;
    }

private:
    /**
     * @brief 若已消费区域较大则压缩内部存储
     *
     * @details 将可读数据移动到向量前端并裁剪尾部。
     * 当读偏移 >= 4096 或已消费区域超过向量大小的一半时触发。
     */
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

    std::vector<char> m_storage;      ///< 连续字节存储
    size_t m_read_offset = 0;         ///< 第一个可读字节的偏移
};

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_BYTE_QUEUE_VIEW_H
