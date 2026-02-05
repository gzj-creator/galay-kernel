#ifndef GALAY_KERNEL_CONCEPTS_H
#define GALAY_KERNEL_CONCEPTS_H

#include <chrono>
#include <concepts>
#include <coroutine>
#include <type_traits>

namespace galay::kernel::concepts
{

template <typename T>
concept ChronoDuration =
    requires {
        typename std::remove_cvref_t<T>::rep;
        typename std::remove_cvref_t<T>::period;
    } &&
    std::same_as<
        std::remove_cvref_t<T>,
        std::chrono::duration<
            typename std::remove_cvref_t<T>::rep,
            typename std::remove_cvref_t<T>::period>>;

template <typename T>
concept Awaitable = std::movable<T> && requires(T t) {
    { t.await_ready() } -> std::convertible_to<bool>;
    t.await_resume();
};

template <typename T, typename Promise>
concept AwaitableWith = Awaitable<T> && requires(T t, std::coroutine_handle<Promise> handle) {
    t.await_suspend(handle);
};

} // namespace galay::kernel::concepts

#endif // GALAY_KERNEL_CONCEPTS_H
