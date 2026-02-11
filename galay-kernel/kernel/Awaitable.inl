#ifndef GALAY_KERNEL_AWAITABLE_INL
#define GALAY_KERNEL_AWAITABLE_INL

#include "Awaitable.h"

namespace galay::kernel {


inline bool AcceptAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return AcceptActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<GHandle, IOError> AcceptAwaitable::await_resume() {
    AcceptActionResume(m_controller);
    return std::move(m_result);
}

inline bool RecvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return RecvActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<Bytes, IOError> RecvAwaitable::await_resume() {
    RecvActionResume(m_controller);
    return std::move(m_result);
}

inline bool SendAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return SendActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> SendAwaitable::await_resume() {
    SendActionResume(m_controller);
    return std::move(m_result);
}

inline bool ReadvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return ReadvActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> ReadvAwaitable::await_resume() {
    ReadvActionResume(m_controller);
    return std::move(m_result);
}

inline bool WritevAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return WritevActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> WritevAwaitable::await_resume() {
    WritevActionResume(m_controller);
    return std::move(m_result);
}

inline bool ConnectAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return ConnectActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<void, IOError> ConnectAwaitable::await_resume() {
    ConnectActionResume(m_controller);
    return std::move(m_result);
}

inline bool CloseAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return CloseActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<void, IOError> CloseAwaitable::await_resume() {
    return std::move(m_result);
}

inline bool FileReadAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return FileReadActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<Bytes, IOError> FileReadAwaitable::await_resume() {
    FileReadActionResume(m_controller);
    return std::move(m_result);
}

inline bool FileWriteAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return FileWriteActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> FileWriteAwaitable::await_resume() {
    FileWriteActionResume(m_controller);
    return std::move(m_result);
}

inline bool RecvFromAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return RecvFromActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<Bytes, IOError> RecvFromAwaitable::await_resume() {
    RecvFromActionResume(m_controller);
    return std::move(m_result);
}

inline bool SendToAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return SendToActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> SendToAwaitable::await_resume() {
    SendToActionResume(m_controller);
    return std::move(m_result);
}

inline bool FileWatchAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return FileWatchActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<FileWatchResult, IOError> FileWatchAwaitable::await_resume() {
    FileWatchActionResume(m_controller);
    return std::move(m_result);
}

inline bool RecvNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return RecvNotifyActionSuspend(this, m_controller, m_waker);
}

inline void RecvNotifyAwaitable::await_resume() {
    RecvNotifyActionResume(m_controller);
}

inline bool SendNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return SendNotifyActionSuspend(this, m_controller, m_waker);
}

inline void SendNotifyAwaitable::await_resume() {
    SendNotifyActionResume(m_controller);
}

inline bool SendFileAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return SendFileActionSuspend(this, m_controller, m_waker, m_result);
}

inline std::expected<size_t, IOError> SendFileAwaitable::await_resume() {
    SendFileActionResume(m_controller);
    return std::move(m_result);
}


// ============ handleComplete inline implementations ============

#ifdef USE_IOURING

inline bool AcceptAwaitable::handleComplete(std::expected<GHandle, IOError>&& result, Host&& host) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    *m_host = std::move(host);
    return true;
}

inline bool RecvAwaitable::handleComplete(std::expected<Bytes, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool SendAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool ReadvAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool WritevAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool ConnectAwaitable::handleComplete(std::expected<void, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool RecvFromAwaitable::handleComplete(std::expected<Bytes, IOError>&& result, Host&& from) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    if(m_from) { *m_from = std::move(from); }
    return true;
}

inline bool SendToAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileReadAwaitable::handleComplete(std::expected<Bytes, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileWriteAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileWatchAwaitable::handleComplete(std::expected<FileWatchResult, IOError>&& result) {
    m_result = std::move(result);
    return true;
}

inline bool SendFileAwaitable::handleComplete(std::expected<size_t, IOError>&& result) {
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

#else // kqueue / epoll

inline bool AcceptAwaitable::handleComplete() {
    auto [result, host] = io::handleAccept(m_controller->m_handle);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    *m_host = std::move(host);
    return true;
}

inline bool RecvAwaitable::handleComplete() {
    auto result = io::handleRecv(m_controller->m_handle, m_buffer, m_length);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool SendAwaitable::handleComplete() {
    auto result = io::handleSend(m_controller->m_handle, m_buffer, m_length);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool ReadvAwaitable::handleComplete() {
    auto result = io::handleReadv(m_controller->m_handle, m_iovecs.data(), static_cast<int>(m_iovecs.size()));
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool WritevAwaitable::handleComplete() {
    auto result = io::handleWritev(m_controller->m_handle, m_iovecs.data(), static_cast<int>(m_iovecs.size()));
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool ConnectAwaitable::handleComplete() {
    auto result = io::handleConnect(m_controller->m_handle, m_host);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool RecvFromAwaitable::handleComplete() {
    auto [result, from] = io::handleRecvFrom(m_controller->m_handle, m_buffer, m_length);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    if(m_from) { *m_from = std::move(from); }
    return true;
}

inline bool SendToAwaitable::handleComplete() {
    auto result = io::handleSendTo(m_controller->m_handle, m_buffer, m_length, m_to);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileReadAwaitable::handleComplete() {
    auto result = io::handleFileRead(m_controller->m_handle, m_buffer, m_length, m_offset);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileWriteAwaitable::handleComplete() {
    auto result = io::handleFileWrite(m_controller->m_handle, m_buffer, m_length, m_offset);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

inline bool FileWatchAwaitable::handleComplete() {
    return true;
}

inline bool SendFileAwaitable::handleComplete() {
    auto result = io::handleSendFile(m_controller->m_handle, m_file_fd, m_offset, m_count);
    if(!result && IOError::contains(result.error().code(), kNotReady)) return false;
    m_result = std::move(result);
    return true;
}

#endif // USE_IOURING

}

#endif