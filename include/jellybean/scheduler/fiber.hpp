#pragma once
#include <coroutine>
#include <exception>
#include <utility>

namespace jellybean::reactor {
class Reactor;
}

namespace jellybean::scheduler {

/**
 * @brief Thread-local lookup for the active Event Loop Reactor.
 *
 * Crucial in cooperative fiber systems to register suspension handles
 * onto the current core's event loop reactor immediately.
 */
auto current_reactor() -> jellybean::reactor::Reactor*;

/**
 * @brief Cooperative yield awaitable structure.
 *
 * Yielding suspends a running coroutine task and enqueues its handle back into
 * the scheduler's run queue, allowing other concurrent fibers to run.
 *
 * Coroutine Awaiter Lifecycle:
 *
 *   1. await_ready() -> false:
 *      Forces the coroutine to suspend, bypassing any fast path.
 *
 *   2. await_suspend(handle) -> void:
 *      The C++ compiler yields execution here, passing the coroutine's suspended state
 *      frame pointer (`std::coroutine_handle<>`). We register this handle with the active
 *      Reactor to be scheduled for the next round-robin tick.
 *
 *   3. await_resume() -> void:
 *      Does nothing. Execution resumes cleanly when the scheduler calls `resume()` on the handle.
 */
struct Yield {
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }
    auto await_suspend(std::coroutine_handle<> h) noexcept -> void;
    auto await_resume() noexcept -> void {}
};

/**
 * @brief Lightweight cooperative Task coroutine container.
 *
 * Coroutine tasks represent cooperative fibers. They are highly efficient, having
 * zero-overhead stack footprints (a few bytes containing the coroutine state frame) compared
 * to heavy OS threads (which require 2MB-8MB of pre-allocated stack space).
 *
 * @tparam T Return type of the asynchronous task (currently void).
 */
template <typename T = void>
struct Task {
    /**
     * @brief The compiler-facing Promise class managing coroutine lifecycles.
     */
    struct promise_type {
        T value_{};

        /**
         * @brief Invoked by compiler on coroutine initialization to construct the Task wrapper.
         */
        auto get_return_object() -> Task {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /**
         * @brief Coroutines are initialized suspended (`std::suspend_always`) so the scheduler
         * can pin them to workers before their first execution tick.
         */
        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }

        /**
         * @brief Keep coroutine frame suspended on completion so the Task wrapper can safely
         * extract results or destroy the frame.
         */
        auto final_suspend() noexcept -> std::suspend_always {
            return {};
        }

        /**
         * @brief Handles unhandled exceptions inside the fiber. Standard serves terminate to avoid
         * silent state corruption.
         */
        void unhandled_exception() {
            std::terminate();
        }

        /**
         * @brief Stores the returned value when `co_return expr` is encountered.
         */
        void return_value(T v) {
            value_ = std::move(v);
        }
    };

    std::coroutine_handle<promise_type> handle;  // Underlying compiler coroutine handle

    Task() noexcept : handle(nullptr) {}
    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}

    // Task cannot be copied to prevent duplicate destructions of the compiler frame.
    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    // Moving transfers ownership of the coroutine handle safely.
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    auto operator=(Task&& other) noexcept -> Task& {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    /**
     * @brief Relinquishes ownership of the coroutine handle without destroying the frame.
     */
    auto release() noexcept -> std::coroutine_handle<promise_type> {
        return std::exchange(handle, nullptr);
    }

    /**
     * @brief Destructor safely destroys the coroutine frame if we still own it.
     */
    ~Task() {
        if (handle) handle.destroy();
    }

    // ---- Awaitable interface so Task<T> can be co_await-ed ----

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return handle && handle.done();
    }

    auto await_suspend(std::coroutine_handle<> caller) noexcept -> std::coroutine_handle<> {
        // Resume the inner task; when it finishes it will suspend at final_suspend,
        // and the caller will be resumed by the scheduler on the next tick.
        // Simple approach: drive the inner task to completion inline then return
        // the caller so it resumes immediately.
        handle.resume();
        return caller;
    }

    auto await_resume() -> T {
        return std::move(handle.promise().value_);
    }
};

/**
 * @brief Explicit void specialisation — keeps return_void() and no stored value.
 */
template <>
struct Task<void> {
    struct promise_type {
        auto get_return_object() -> Task {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        auto initial_suspend() noexcept -> std::suspend_always { return {}; }
        auto final_suspend() noexcept -> std::suspend_always { return {}; }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
    };

    std::coroutine_handle<promise_type> handle;

    Task() noexcept : handle(nullptr) {}
    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    auto operator=(Task&& other) noexcept -> Task& {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    auto release() noexcept -> std::coroutine_handle<promise_type> {
        return std::exchange(handle, nullptr);
    }

    ~Task() {
        if (handle) handle.destroy();
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return handle && handle.done();
    }
    auto await_suspend(std::coroutine_handle<> caller) noexcept -> std::coroutine_handle<> {
        handle.resume();
        return caller;
    }
    auto await_resume() noexcept -> void {}
};

}  // namespace jellybean::scheduler
