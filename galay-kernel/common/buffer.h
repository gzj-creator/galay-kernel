/**
 * @file buffer.h
 * @brief 用于 I/O 操作的内存缓冲区与环形缓冲区
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 为 galay-kernel I/O 子系统提供两种缓冲区抽象：
 * - StringMetaData：底层字符串元数据（指针、大小、容量）
 * - Buffer：支持动态增长和移动语义的线性字节缓冲区
 * - RingBuffer：固定容量的环形缓冲区，针对 scatter-gather I/O（readv/writev）优化
 */

#ifndef GALAY_BUFFER_H
#define GALAY_BUFFER_H

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <cstdint>
#include <sys/types.h>
#include <sys/uio.h>

namespace galay::kernel
{
    /**
     * @brief 底层字符串元数据结构
     * @details 管理原始数据指针及其大小和容量
     */
    struct StringMetaData
    {
        StringMetaData() {};

        /**
         * @brief 从 std::string 构造（非拥有视图）
         * @param str 源字符串
         */
        StringMetaData(std::string& str);

        /**
         * @brief 从 std::string_view 构造（非拥有视图）
         * @param str 字符串视图
         */
        StringMetaData(const std::string_view& str);

        /**
         * @brief 从 C 风格字符串构造（非拥有视图）
         * @param str C 字符串指针
         */
        StringMetaData(const char* str);

        /**
         * @brief 从字节数组构造（非拥有视图）
         * @param str 字节数组指针
         */
        StringMetaData(const uint8_t* str);

        /**
         * @brief 从原始指针和显式长度构造
         * @param str C 字符串指针
         * @param length 字节数
         */
        StringMetaData(const char* str, size_t length);

        /**
         * @brief 从原始字节指针和显式长度构造
         * @param str 字节数组指针
         * @param length 字节数
         */
        StringMetaData(const uint8_t* str, size_t length);

        /**
         * @brief 移动构造函数
         */
        StringMetaData(StringMetaData&& other);

        /**
         * @brief 移动赋值运算符
         */
        StringMetaData& operator=(StringMetaData&& other);

        ~StringMetaData();

        uint8_t* data = nullptr;    ///< 数据指针
        size_t size = 0;             ///< 当前数据大小（字节）
        size_t capacity = 0;         ///< 已分配容量（字节）
    };

    /**
     * @brief 分配指定长度的 StringMetaData 缓冲区
     * @param length 要分配的字节数
     * @return 容量已设置、大小初始化为 0 的 StringMetaData
     */
    StringMetaData mallocString(size_t length);

    /**
     * @brief 深拷贝 StringMetaData 到新分配的缓冲区
     * @param meta 源元数据
     * @return 拥有独立数据副本的新 StringMetaData
     */
    StringMetaData deepCopyString(const StringMetaData& meta);

    /**
     * @brief 重新分配 StringMetaData 缓冲区到新大小
     * @param meta 要重新分配的元数据
     * @param length 新容量（字节）；若为 0 则释放缓冲区
     */
    void reallocString(StringMetaData& meta, size_t length);

    /**
     * @brief 将数据清零但不释放；大小重置为 0
     * @param meta 要清除的元数据
     */
    void clearString(StringMetaData& meta);

    /**
     * @brief 释放 StringMetaData 持有的缓冲区并重置所有字段
     * @param meta 数据指针将被释放的元数据
     */
    void freeString(StringMetaData& meta);

    /**
     * @brief 支持动态增长和移动语义的线性字节缓冲区
     * @details 提供高效的内存缓冲区管理，支持动态重新分配和仅移动语义以避免拷贝。
     */
    class Buffer
    {
    public:
        /**
         * @brief 默认构造空缓冲区，不分配存储空间
         */
        Buffer();

        /**
         * @brief 以指定容量构造缓冲区
         * @param capacity 初始容量（字节）
         */
        Buffer(size_t capacity);

        /**
         * @brief 从原始数据拷贝构造
         * @param data 源指针
         * @param size 要拷贝的字节数
         */
        Buffer(const void* data, size_t size);

        /**
         * @brief 从 std::string 拷贝构造
         * @param str 源字符串
         */
        Buffer(const std::string& str);

        /**
         * @brief 清除缓冲区内容（内存清零，保留分配）
         */
        void clear();

        /**
         * @brief 获取数据的可变指针
         * @return 指向数据起始位置的 char 指针
         */
        char *data();

        /**
         * @brief 获取数据的常量指针
         * @return 指向数据起始位置的 const char 指针
         */
        const char *data() const;

        /**
         * @brief 获取当前存储的字节数
         * @return 数据大小（字节）
         */
        size_t length() const;

        /**
         * @brief 获取已分配的容量
         * @return 容量（字节）
         */
        size_t capacity() const;

        /**
         * @brief 通过 realloc 调整缓冲区大小
         * @param capacity 新容量（字节）；0 表示释放内存
         */
        void resize(size_t capacity);

        /**
         * @brief 将缓冲区内容拷贝为 std::string
         * @return 包含缓冲区数据的新字符串
         */
        std::string toString() const;

        /**
         * @brief 获取数据的零拷贝 string_view
         * @return 引用缓冲区内容的 string_view
         */
        std::string_view toStringView() const;

        /**
         * @brief 移动赋值运算符
         */
        Buffer& operator=(Buffer&& other);

        ~Buffer();

        /**
         * @brief 与另一个缓冲区交换内容
         * @param other 要交换的缓冲区
         */
        void swap(Buffer& other) {
            std::swap(m_data, other.m_data);
        }

    private:
        StringMetaData m_data;
    };

    /**
     * @brief 固定容量的环形缓冲区，用于 scatter-gather 网络 I/O
     *
     * @details 支持环绕式读写，提供简洁的接口：
     *
     * 内存布局示例（容量=1000，读位置=800，写位置=200，已环绕）：
     * +------------------+--------+------------------+
     * |     0-200        | 200-800|    800-1000      |
     * |     可读         | 可写   |     可读         |
     * +------------------+--------+------------------+
     *
     * 特性：
     * - 固定容量，不自动增长
     * - 环绕式读写
     * - getWriteIovecs() 返回 1-2 个 iovec 用于 readv
     * - getReadIovecs() 返回 1-2 个 iovec 用于 writev
     */
    class RingBuffer
    {
    public:
        static constexpr size_t kDefaultCapacity = 4096;

        /**
         * @brief 以指定固定容量构造环形缓冲区
         * @param capacity 缓冲区大小（字节），固定不变，不增长
         */
        explicit RingBuffer(size_t capacity = kDefaultCapacity);

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        RingBuffer(RingBuffer&& other) noexcept;
        RingBuffer& operator=(RingBuffer&& other) noexcept;

        ~RingBuffer();

        // ============ 基本状态查询 ============

        size_t readable() const { return m_size; }              ///< 可读字节数
        size_t writable() const { return m_capacity - m_size; } ///< 可写字节数
        size_t capacity() const { return m_capacity; }          ///< 缓冲区总容量
        bool empty() const { return m_size == 0; }              ///< 是否无可读数据
        bool full() const { return m_size == m_capacity; }      ///< 是否无可写空间

        // ============ 核心接口 ============

        /**
         * @brief 获取可写区域的 iovec 描述符（用于 readv）
         * @param out 输出 iovec 数组
         * @param max_iovecs 数组容量；最多使用 2 个槽位
         * @return 填充的 iovec 条目数
         *
         * @code
         * std::array<struct iovec, 2> iovecs{};
         * size_t count = buffer.getWriteIovecs(iovecs);
         * ssize_t n = co_await socket.readv(iovecs, count);
         * buffer.produce(n);
         * @endcode
         */
        size_t getWriteIovecs(struct iovec* out, size_t max_iovecs = 2) const;

        /**
         * @brief 获取可写区域的 iovec 描述符（std::array 重载）
         * @tparam N 数组大小
         * @param out 输出 iovec 数组
         * @return 填充的 iovec 条目数
         */
        template<size_t N>
        size_t getWriteIovecs(std::array<struct iovec, N>& out) const {
            return getWriteIovecs(out.data(), N);
        }

        /**
         * @brief 获取可读区域的 iovec 描述符（用于 writev）
         * @param out 输出 iovec 数组
         * @param max_iovecs 数组容量；最多使用 2 个槽位
         * @return 填充的 iovec 条目数
         *
         * @code
         * std::array<struct iovec, 2> iovecs{};
         * size_t count = buffer.getReadIovecs(iovecs);
         * ssize_t n = co_await socket.writev(iovecs, count);
         * buffer.consume(n);
         * @endcode
         */
        size_t getReadIovecs(struct iovec* out, size_t max_iovecs = 2) const;

        /**
         * @brief 获取可读区域的 iovec 描述符（std::array 重载）
         * @tparam N 数组大小
         * @param out 输出 iovec 数组
         * @return 填充的 iovec 条目数
         */
        template<size_t N>
        size_t getReadIovecs(std::array<struct iovec, N>& out) const {
            return getReadIovecs(out.data(), N);
        }

        /**
         * @brief 确认已写入的字节数并推进写指针
         * @param len 已写入的字节数
         */
        void produce(size_t len);

        /**
         * @brief 消费字节并推进读指针
         * @param len 要消费的字节数
         */
        void consume(size_t len);

        /**
         * @brief 清空缓冲区（重置读写指针，不释放内存）
         */
        void clear();

        // ============ 便捷方法 ============

        /**
         * @brief 将数据拷贝到环形缓冲区
         * @param data 源指针
         * @param len 要写入的字节数
         * @return 实际写入的字节数（缓冲区满时可能少于请求量）
         */
        size_t write(const void* data, size_t len);

        /**
         * @brief 将 string_view 写入环形缓冲区
         * @param str 要写入的数据
         * @return 实际写入的字节数
         */
        size_t write(const std::string_view& str) {
            return write(str.data(), str.size());
        }

    private:
        char* m_buffer;         ///< 底层存储
        size_t m_capacity;      ///< 总容量（字节）
        size_t m_readIndex;     ///< 读指针位置
        size_t m_writeIndex;    ///< 写指针位置
        size_t m_size;          ///< 当前可读数据大小
    };
}

#endif
