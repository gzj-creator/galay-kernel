#ifndef GALAY_KERNEL_BACKEND_REACTOR_H
#define GALAY_KERNEL_BACKEND_REACTOR_H

#include "galay-kernel/common/Error.h"

#include <atomic>
#include <optional>

namespace galay::kernel {

class BackendReactor
{
public:
    virtual ~BackendReactor() = default;
    virtual void notify() = 0;
    virtual int wakeReadFdForTest() const = 0;
};

namespace detail {

inline void storeBackendError(std::atomic<uint64_t>& last_error_code,
                              IOErrorCode error_code,
                              uint32_t system_code) noexcept {
    last_error_code.store(IOError(error_code, system_code).code(), std::memory_order_release);
}

inline auto loadBackendError(const std::atomic<uint64_t>& last_error_code)
    -> std::optional<IOError> {
    const uint64_t code = last_error_code.load(std::memory_order_acquire);
    if (code == 0) {
        return std::nullopt;
    }
    return IOError(static_cast<IOErrorCode>(code & 0xffffffffu),
                   static_cast<uint32_t>(code >> 32));
}

}  // namespace detail

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_BACKEND_REACTOR_H
