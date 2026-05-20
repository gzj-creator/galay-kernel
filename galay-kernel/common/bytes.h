/**
 * @file bytes.h
 * @brief 仅移动的字节序列容器，支持可选所有权
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details Bytes 类封装一个 StringMetaData 和一个所有权标志。
 * 当拥有所有权（m_owned == true）时，析构函数释放底层内存；
 * 当不拥有所有权时，充当非拥有视图（如 fromCString、fromString）。
 * 仅移动：拷贝构造和拷贝赋值已删除，以防止 I/O 路径中的意外深拷贝。
 */

#ifndef GALAY_BYTES_H
#define GALAY_BYTES_H

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include "buffer.h"

namespace galay::kernel
{
    /**
     * @brief 仅移动的字节序列容器，支持可选所有权
     *
     * @details 用于网络 I/O 和数据传输的高效字节容器。
     * 封装 StringMetaData 和所有权标志（m_owned）。
     * 拥有所有权时析构函数释放内存；否则充当非拥有视图。
     * 使用 StringMetaData + m_owned 而非 variant 以消除分支开销。
     */
    class Bytes
    {
    public:
        /**
         * @brief 默认构造空字节容器
         */
        Bytes() {};

        /**
         * @brief 从 std::string 深拷贝构造
         * @param str 源字符串；数据拷贝到新缓冲区
         */
        Bytes(std::string& str);

        /**
         * @brief 从右值 std::string 深拷贝构造
         * @param str 源字符串（数据仍然被拷贝）
         */
        Bytes(std::string&& str);

        /**
         * @brief 从 C 风格字符串深拷贝构造
         * @param str C 字符串指针
         */
        Bytes(const char* str);

        /**
         * @brief 从字节数组深拷贝构造
         * @param str 字节数组指针
         */
        Bytes(const uint8_t* str);

        /**
         * @brief 从 char 指针和显式长度深拷贝构造
         * @param str 源指针
         * @param length 字节数
         */
        Bytes(const char* str, size_t length);

        /**
         * @brief 从字节指针和显式长度深拷贝构造
         * @param str 源指针
         * @param length 字节数
         */
        Bytes(const uint8_t* str, size_t length);

        /**
         * @brief 分配指定容量的拥有缓冲区，大小为 0
         * @param capacity 要分配的字节数
         */
        Bytes(size_t capacity);

        /**
         * @brief 移动构造函数
         */
        Bytes(Bytes&& other) noexcept;

        /**
         * @brief 拷贝构造已删除，防止意外拷贝
         */
        Bytes(const Bytes& other) = delete;

        /**
         * @brief 移动赋值运算符
         */
        Bytes& operator=(Bytes&& other) noexcept;

        /**
         * @brief 拷贝赋值已删除，防止意外拷贝
         */
        Bytes& operator=(const Bytes& other) = delete;

        ~Bytes();

        /**
         * @brief 创建 std::string 上的非拥有 Bytes 视图
         * @param str 源字符串（必须比返回的 Bytes 存活更久）
         * @return 非拥有 Bytes
         */
        static Bytes fromString(std::string& str);

        /**
         * @brief 创建 std::string_view 上的非拥有 Bytes 视图
         * @param str 字符串视图（底层数据必须比返回的 Bytes 存活更久）
         * @return 非拥有 Bytes
         */
        static Bytes fromString(const std::string_view& str);

        /**
         * @brief 创建 C 字符串上具有显式长度和容量的非拥有 Bytes 视图
         * @param str 源指针（必须比返回的 Bytes 存活更久）
         * @param length 可读字节数
         * @param capacity 总分配大小
         * @return 非拥有 Bytes
         */
        static Bytes fromCString(const char* str, size_t length, size_t capacity);

        /**
         * @brief 获取字节数据的常量指针
         * @return 指向首字节的指针，若为空则返回 nullptr
         */
        const uint8_t* data() const noexcept;

        /**
         * @brief 获取以 null 结尾的 C 字符串指针
         * @return const char 指针，若无数据则返回 nullptr
         */
        const char* c_str() const noexcept;

        /**
         * @brief 获取存储的字节数
         * @return 字节数
         */
        size_t size() const noexcept;

        /**
         * @brief 获取已分配的容量
         * @return 容量（字节）
         */
        size_t capacity() const noexcept;

        /**
         * @brief 检查容器是否不持有数据
         * @return 若大小为 0 则返回 true
         */
        bool empty() const noexcept;

        /**
         * @brief 释放拥有的内存并重置为空状态
         */
        void clear() noexcept;

        /**
         * @brief 将字节数据拷贝为 std::string
         * @return 新字符串，若无数据则返回空字符串
         */
        std::string toString() const;

        /**
         * @brief 获取字节数据的零拷贝 string_view
         * @return string_view，若无数据则返回空 string_view
         */
        std::string_view toStringView() const;

        /**
         * @brief 比较字节内容是否相等
         * @param other 要比较的 Bytes
         * @return 若两者大小相同且内容一致（或指针相同）则返回 true
         */
        bool operator==(const Bytes& other) const;

        /**
         * @brief 比较字节内容是否不相等
         * @param other 要比较的 Bytes
         * @return 若大小或字节内容不同则返回 true
         */
        bool operator!=(const Bytes& other) const;
    private:
        StringMetaData m_meta;      ///< 数据元数据（指针、大小、容量）
        bool m_owned{false};        ///< 若本对象拥有内存则为 true；析构函数将释放
    };
}


#endif
