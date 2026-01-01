#include "Waker.h"

namespace galay::kernel {

Waker::Waker(std::coroutine_handle<> handle)
    : m_coroutine(std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address()).promise().getCoroutine())
{
}

Waker::Waker(const Waker& other)
    : m_coroutine(other.m_coroutine)
{
}

Waker::Waker(Waker&& waker)
    : m_coroutine(waker.m_coroutine)  // 使用拷贝而不是移动
{
}

Waker& Waker::operator=(const Waker& other)
{
    if (this != &other) {
        m_coroutine = other.m_coroutine;
    }
    return *this;
}

Waker& Waker::operator=(Waker&& other)
{
    if (this != &other) {
        m_coroutine = other.m_coroutine;  // 使用拷贝而不是移动
    }
    return *this;
}

void Waker::wakeUp()
{
    m_coroutine.resume();
}

}