#pragma once

#include <coroutine>
#include <future>
#include <thread>

#include "jellybean/reactor/continuation_pool.hpp"

namespace jellybean::reactor {

/**
 * @brief Zero-spin awaitable for std::future<T>.
 *
 * This uses the ContinuationPool to block on the future off-reactor,
 * avoiding the overhead of spawning detached threads per request.
 *
 * @tparam T  Return type of the future.
 */
template <typename T>
struct FutureAwaitable {
    std::future<T> fut;
    Reactor* reactor;  // captured at await_suspend time (always non-null on reactor thread)
    T result{};

    explicit FutureAwaitable(std::future<T>&& f) : fut(std::move(f)), reactor(Reactor::current()) {}

    auto await_ready() noexcept -> bool {
        // Fast path: already resolved (e.g. error response, queue timeout).
        return fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void await_suspend(std::coroutine_handle<> h) {
        // Move the future into the pool so it outlives this awaitable.
        auto shared_fut = std::make_shared<std::future<T>>(std::move(fut));
        ContinuationPool::instance().post([h, r = reactor, f = shared_fut, this]() mutable {
            result = f->get();  // Block only on the pool thread.
            r->schedule(h);    // Wake the suspended coroutine.
        });
    }

    auto await_resume() -> T {
        // If await_ready() was true, fut still valid; extract directly.
        if (fut.valid()) {
            return fut.get();
        }
        return std::move(result);
    }
};

// Deduction guide
template <typename T>
FutureAwaitable(std::future<T>&&) -> FutureAwaitable<T>;

/**
 * Helper: co_await infer_future(runtime.infer_async(req))
 * Usage in session():
 *   InferenceResponse resp = co_await infer_future(runtime.infer_async(req));
 */
template <typename T>
auto infer_future(std::future<T>&& f) -> FutureAwaitable<T> {
    return FutureAwaitable<T>{std::move(f)};
}

}  // namespace jellybean::reactor

// ─── Production upgrade note ────────────────────────────────────────────────
// The detached-thread approach is correct but spawns one thread per request.
// For ultra-high throughput (>10k RPS), replace with:
//   - A shared "continuation thread pool" (e.g. 2-4 threads) that
//     sits on a SPSC queue of (future, coroutine_handle) pairs and
//     calls reactor->schedule(h) as each future fires.
//   - Or use infer_async() to accept a std::function<void(InferenceResponse)>
//     completion callback, called directly from the worker thread after
//     batch dispatch, completely eliminating the intermediate future.
// ────────────────────────────────────────────────────────────────────────────
