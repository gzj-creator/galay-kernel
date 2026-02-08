#include "Bytes.h"

namespace galay::kernel
{
    Bytes::Bytes(std::string &str)
    {
        m_meta.size = str.size();
        m_meta.capacity = str.size();
        m_meta.data = (uint8_t*)malloc(str.size());
        std::memcpy(m_meta.data, str.data(), str.size());
        m_owned = true;
    }

    Bytes::Bytes(std::string &&str)
    {
        m_meta.size = str.size();
        m_meta.capacity = str.size();
        m_meta.data = (uint8_t*)malloc(str.size());
        std::memcpy(m_meta.data, str.data(), str.size());
        m_owned = true;
    }

    Bytes::Bytes(const char *str)
    {
        size_t len = std::strlen(str);
        m_meta.size = len;
        m_meta.capacity = len;
        m_meta.data = (uint8_t*)malloc(len);
        std::memcpy(m_meta.data, str, len);
        m_owned = true;
    }

    Bytes::Bytes(const uint8_t *str)
    {
        size_t len = std::strlen(reinterpret_cast<const char*>(str));
        m_meta.size = len;
        m_meta.capacity = len;
        m_meta.data = (uint8_t*)malloc(len);
        std::memcpy(m_meta.data, str, len);
        m_owned = true;
    }

    Bytes::Bytes(const char *str, size_t length)
    {
        m_meta.size = length;
        m_meta.capacity = length;
        m_meta.data = (uint8_t*)malloc(length);
        std::memcpy(m_meta.data, str, length);
        m_owned = true;
    }

    Bytes::Bytes(const uint8_t *str, size_t length)
    {
        m_meta.size = length;
        m_meta.capacity = length;
        m_meta.data = (uint8_t*)malloc(length);
        std::memcpy(m_meta.data, str, length);
        m_owned = true;
    }

    Bytes::Bytes(size_t capacity)
    {
        m_meta.size = 0;
        m_meta.capacity = capacity;
        m_meta.data = (uint8_t*)malloc(capacity);
        m_owned = true;
    }

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

    Bytes::~Bytes()
    {
        if (m_owned && m_meta.data) {
            free(m_meta.data);
        }
    }

    Bytes Bytes::fromString(std::string &str)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(str.data());
        bytes.m_meta.size = str.size();
        bytes.m_meta.capacity = str.capacity();
        bytes.m_owned = false;
        return bytes;
    }

    Bytes Bytes::fromString(const std::string_view &str)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(const_cast<char*>(str.data()));
        bytes.m_meta.size = str.size();
        bytes.m_meta.capacity = str.size();
        bytes.m_owned = false;
        return bytes;
    }

    Bytes Bytes::fromCString(const char *str, size_t length, size_t capacity)
    {
        Bytes bytes;
        bytes.m_meta.data = reinterpret_cast<uint8_t*>(const_cast<char*>(str));
        bytes.m_meta.size = length;
        bytes.m_meta.capacity = capacity;
        bytes.m_owned = false;
        return bytes;
    }

    const uint8_t* Bytes::data() const noexcept
    {
        return m_meta.data;
    }

    const char* Bytes::c_str() const noexcept
    {
        if (!m_meta.data) return nullptr;
        if (m_meta.size > 0 && m_meta.data[m_meta.size - 1] != '\0') {
            m_meta.data[m_meta.size] = '\0';
        }
        return reinterpret_cast<const char*>(m_meta.data);
    }

    size_t Bytes::size() const noexcept
    {
        return m_meta.size;
    }

    size_t Bytes::capacity() const noexcept
    {
        return m_meta.capacity;
    }

    bool Bytes::empty() const noexcept
    {
        return m_meta.size == 0;
    }

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

    std::string Bytes::toString() const
    {
        if (!m_meta.data || m_meta.size == 0) return "";
        return std::string(reinterpret_cast<const char*>(m_meta.data), m_meta.size);
    }

    std::string_view Bytes::toStringView() const
    {
        if (!m_meta.data || m_meta.size == 0) return std::string_view();
        return std::string_view(reinterpret_cast<const char*>(m_meta.data), m_meta.size);
    }

    bool Bytes::operator==(const Bytes &other) const
    {
        return m_meta.size == other.m_meta.size &&
               (m_meta.data == other.m_meta.data ||
                (m_meta.data && other.m_meta.data &&
                 std::memcmp(m_meta.data, other.m_meta.data, m_meta.size) == 0));
    }

    bool Bytes::operator!=(const Bytes &other) const
    {
        return !operator==(other);
    }

    StringMetaData mallocString(size_t length)
    {
        StringMetaData metaData;
        metaData.capacity = length;
        metaData.data = (uint8_t*)malloc(length);
        metaData.size = 0;
        return metaData;
    }

    StringMetaData deepCopyString(const StringMetaData& meta)
    {
        StringMetaData metaData;
        metaData = mallocString(meta.capacity);
        metaData.size = meta.size;
        memcpy(metaData.data, meta.data, meta.size);
        return metaData;
    }

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

    void clearString(StringMetaData &meta)
    {
        meta.size = 0;
        memset(meta.data, 0, meta.capacity);
    }

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
