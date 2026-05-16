#pragma once
#include <coroutine>
#include <exception>
#include <utility>

namespace jellybean::reactor {
    class Reactor;
}

namespace jellybean::scheduler {

/**
 * @brief Gets the current thread's reactor.
 */
jellybean::reactor::Reactor* current_reactor();

/**
 * @brief Yields control back to the reactor.
 */
struct Yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}
};

/**
 * @brief A task that can be awaited.
 */
template<typename T = void>
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
        
        // Support for return T if needed
        // void return_value(T value) { ... }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task() noexcept : handle(nullptr) {}
    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    std::coroutine_handle<promise_type> release() noexcept {
        return std::exchange(handle, nullptr);
    }

    ~Task() {
        if (handle) handle.destroy();
    }
};

} // namespace jellybean::scheduler
