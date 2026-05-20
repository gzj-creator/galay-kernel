/**
 * @file bytes.cc
 * @brief Bytes 容器和 StringMetaData 辅助函数的实现
 * @author galay-kernel
 * @version 1.0.0
 */

#include "bytes.h"

namespace galay::kernel
{
    /**
     * @brief 从 std::string 深拷贝构造
     * @param str 源字符串；数据拷贝到新 malloc 分配的缓冲区
     */
    Bytes::Bytes(std::string &str)
    {
        m_meta.size = str.size();
        m_meta.capacity = str.size();
        m_meta.data = (uint8_t*)malloc(str.size());
        std::memcpy(m_meta.data, str.data(), str.size());
        m_owned = true;
    }

    /**
     * @brief 从右值 std::string 深拷贝构造
     * @param str 源字符串（已移出，但数据仍然被拷贝）
     */
    Bytes::Bytes(std::string &&str)
    {
        m_meta.size = str.size();
        m_meta.capacity = str.size();
        m_meta.data = (uint8_t*)malloc(str.size());
        std::memcpy(m_meta.data, str.data(), str.size());
        m_owned = true;
    }

    /**
     * @brief 从 C 风格字符串深拷贝构造
     * @param str 源 C 字符串
     */
    Bytes::Bytes(const char *str)
    {
        size_t len = std::strlen(str);
        m_meta.size = len;
        m_meta.capacity = len;
        m_meta.data = (uint8_t*)malloc(len);
        std::memcpy(m_meta.data, str, len);
        m_owned = true;
    }

    /**
     * @brief 从字节数组深拷贝构造
     * @param str 源字节数组
     */
    Bytes::Bytes(const uint8_t *str)
    {
        size_t len = std::strlen(reinterpret_cast<const char*>(str));
        m_meta.size = len;
        m_meta.capacity = len;
        m_meta.data = (uint8_t*)malloc(len);
        std::memcpy(m_meta.data, str, len);
        m_owned = true;
    }

    /**
     * @brief 从 char 指针和显式长度深拷贝构造
     * @param str 源指针
     * @param length 要拷贝的字节数
     */
    Bytes::Bytes(const char *str, size_t length)
    {
        m_meta.size = length;
        m_meta.capacity = length;
        m_meta.data = (uint8_t*)malloc(length);
        std::memcpy(m_meta.data, str, length);
        m_owned = true;
    }

    /**
     * @brief 从字节指针和显式长度深拷贝构造
     * @param str 源指针
     * @param length 要拷贝的字节数
     */
    Bytes::Bytes(const uint8_t *str, size_t length)
    {
        m_meta.size = length;
        m_meta.capacity = length;
        m_meta.data = (uint8_t*)malloc(length);
        std::memcpy(m_meta.data, str, length);
        m_owned = true;
    }

    /**
     * @brief 分配指定容量的拥有缓冲区，大小为 0
     * @param capacity 要分配的字节数
     */
    Bytes::Bytes(size_t capacity)
    {
        m_meta.size = 0;
        m_meta.capacity = capacity;
        m_meta.data = (uint8_t*)malloc(capacity);
        m_owned = true;
    }

    /**
     * @brief 移动构造函数；从 other 转移所有权
     * @param other 源 Bytes（移后处于有效的空状态）
     */
    Bytes::Bytes(Bytes &&other) noexcept
    {
        m_meta.data = other.m_meta.data;
        m_meta.size = other.m_meta.size;
        m_meta.capacity = other.m_meta.capacity;
        m_owned = other.m_owned;
        other.m_meta.data = nullptr;
        other.m_meta.size = 0;
        other.m_meta.capacity = 0;
        other.m_owned = false;
    }

    /**
     * @brief 移动赋值；释放当前数据并接管 other 的
     * @param other 源 Bytes（移后为空）
     * @return 本对象的引用
     */
    Bytes &Bytes::operator=(Bytes &&other) noexcept
    {
        if (this != &other) {
            if (m_owned && m_meta.data) {
                free(m_meta.data);
            }
            m_meta.data = other.m_meta.data;
            m_meta.size = other.m_meta.size;
            m_meta.capacity = other.m_meta.capacity;
            m_owned = other.m_owned;
            other.m_meta.data = nullptr;
            other.m_meta.size = 0;
            other.m_meta.capacity = 0;
            other.m_owned = false;
        }
        return *this;
    }

    /**
     * @brief 析构函数；若拥有所有权则释放内部缓冲区
     */
    Bytes::~Bytes()
    {
        if (m_owned && m_meta.data) {
            free(m_meta.data);
        }
    }

    /**
     * @brief 创建 std::string 上的非拥有 Bytes 视图
     * @param str 源字符串（必须比返回的 Bytes 存活更久）
     * @return 非拥有 Bytes
     */
    Bytes Bytes::fromString(std::string &str)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(str.data());
        bytes.m_meta.size = str.size();
        bytes.m_meta.capacity = str.capacity();
        bytes.m_owned = false;
        return bytes;
    }

    /**
     * @brief 创建 std::string_view 上的非拥有 Bytes 视图
     * @param str 源视图（底层数据必须比返回的 Bytes 存活更久）
     * @return 非拥有 Bytes
     */
    Bytes Bytes::fromString(const std::string_view &str)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(const_cast<char*>(str.data()));
        bytes.m_meta.size = str.size();
        bytes.m_meta.capacity = str.size();
        bytes.m_owned = false;
        return bytes;
    }

    /**
     * @brief 创建 C 字符串上具有显式长度和容量的非拥有 Bytes 视图
     * @param str 源指针（必须比返回的 Bytes 存活更久）
     * @param length 可读字节数
     * @param capacity 总分配大小
     * @return 非拥有 Bytes
     */
    Bytes Bytes::fromCString(const char *str, size_t length, size_t capacity)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(const_cast<char*>(str));
        bytes.m_meta.size = length;
        bytes.m_meta.capacity = capacity;
        bytes.m_owned = false;
        return bytes;
    }

    /**
     * @brief 获取字节数据的常量指针
     * @return 指向首字节的指针，若为空则返回 nullptr
     */
    const uint8_t* Bytes::data() const noexcept
    {
        return m_meta.data;
    }

    /**
     * @brief 获取以 null 结尾的 C 字符串指针
     * @return const char 指针，若无数据则返回 nullptr
     *
     * @note 若末尾不存在 null 终止符，则在 m_meta.size 位置写入一个。
     *       需要 capacity > size 以留出终止符的空间。
     */
    const char* Bytes::c_str() const noexcept
    {
        if (!m_meta.data) return nullptr;
        if (m_meta.size > 0 && m_meta.data[m_meta.size - 1] != '\0') {
            m_meta.data[m_meta.size] = '\0';
        }
        return reinterpret_cast<const char*>(m_meta.data);
    }

    /**
     * @brief 获取存储的字节数
     * @return 字节数
     */
    size_t Bytes::size() const noexcept
    {
        return m_meta.size;
    }

    /**
     * @brief 获取已分配的容量
     * @return 容量（字节）
     */
    size_t Bytes::capacity() const noexcept
    {
        return m_meta.capacity;
    }

    /**
     * @brief 检查容器是否不持有数据
     * @return 若大小为 0 则返回 true
     */
    bool Bytes::empty() const noexcept
    {
        return m_meta.size == 0;
    }

    /**
     * @brief 释放拥有的内存并重置为空状态
     */
    void Bytes::clear() noexcept
    {
        if (m_owned && m_meta.data) {
            free(m_meta.data);
        }
        m_meta.data = nullptr;
        m_meta.size = 0;
        m_meta.capacity = 0;
        m_owned = false;
    }

    /**
     * @brief 将字节数据拷贝为 std::string
     * @return 新字符串，若无数据则返回空字符串
     */
    std::string Bytes::toString() const
    {
        if (!m_meta.data || m_meta.size == 0) return "";
        return std::string(reinterpret_cast<const char*>(m_meta.data), m_meta.size);
    }

    /**
     * @brief 获取字节数据的零拷贝 string_view
     * @return string_view，若无数据则返回空 string_view
     */
    std::string_view Bytes::toStringView() const
    {
        if (!m_meta.data || m_meta.size == 0) return std::string_view();
        return std::string_view(reinterpret_cast<const char*>(m_meta.data), m_meta.size);
    }

    /**
     * @brief 比较字节内容是否相等
     * @param other 要比较的 Bytes
     * @return 若两者大小相同且字节内容一致（或指针相同）则返回 true
     */
    bool Bytes::operator==(const Bytes &other) const
    {
        return m_meta.size == other.m_meta.size &&
               (m_meta.data == other.m_meta.data ||
                (m_meta.data && other.m_meta.data &&
                 std::memcmp(m_meta.data, other.m_meta.data, m_meta.size) == 0));
    }

    /**
     * @brief 比较字节内容是否不相等
     * @param other 要比较的 Bytes
     * @return 若大小或字节内容不同则返回 true
     */
    bool Bytes::operator!=(const Bytes &other) const
    {
        return !operator==(other);
    }

    /**
     * @brief 分配指定长度的 StringMetaData 缓冲区
     * @param length 要分配的字节数
     * @return 容量已设置、大小初始化为 0 的 StringMetaData
     */
    StringMetaData mallocString(size_t length)
    {
        StringMetaData metaData;
        metaData.capacity = length;
        metaData.data = (uint8_t*)malloc(length);
        metaData.size = 0;
        return metaData;
    }

    /**
     * @brief 深拷贝 StringMetaData 到新分配的缓冲区
     * @param meta 源元数据
     * @return 拥有独立数据副本的新 StringMetaData
     */
    StringMetaData deepCopyString(const StringMetaData& meta)
    {
        StringMetaData metaData;
        metaData = mallocString(meta.capacity);
        metaData.size = meta.size;
        memcpy(metaData.data, meta.data, meta.size);
        return metaData;
    }

    /**
     * @brief 通过 realloc 调整 StringMetaData 缓冲区大小
     * @param meta 要调整大小的元数据
     * @param length 新容量；若为 0 则释放缓冲区
     * @throws std::bad_alloc 若 realloc 失败
     */
    void reallocString(StringMetaData &meta, size_t length)
    {
        if(length == 0) {
            // 释放内存
            if (meta.data) {
                free(meta.data);
                meta.data = nullptr;
            }
            meta.size = 0;
            meta.capacity = 0;
            return;
        }
        if(meta.size > length) {
            meta.size = length;
        }
        meta.capacity = length;
        meta.data = (uint8_t*)realloc(meta.data, length);
        if (meta.data == nullptr)
        {
            throw std::bad_alloc();
        }
    }

    /**
     * @brief 将数据清零但不释放；大小重置为 0
     * @param meta 要清除的元数据
     */
    void clearString(StringMetaData &meta)
    {
        meta.size = 0;
        memset(meta.data, 0, meta.capacity);
    }

    /**
     * @brief 释放 StringMetaData 持有的缓冲区并重置所有字段
     * @param meta 数据指针将被释放的元数据
     */
    void freeString(StringMetaData &meta)
    {
        if(meta.data != nullptr) {
            free(meta.data);
            meta.data = nullptr;
            meta.capacity = 0;
            meta.size = 0;
        }
    }
}
