#pragma once

#include <coroutine>
#include <future>
#include <thread>

#include "jellybean/reactor/reactor.hpp"

namespace jellybean::reactor {

/**
 * @brief Zero-spin awaitable for std::future<T>.
 *
 * The standard busy-poll pattern:
 *   while (fut.wait_for(10us) != ready) { co_await Yield{}; }
 * burns reactor ticks: each Yield re-queues the coroutine frame and the
 * reactor iterates it again immediately, wasting CPU and delaying OTHER
 * sessions sharing the same reactor thread.
 *
 * A std::condition_variable would be worse — it would BLOCK the reactor
 * thread entirely, freezing every coroutine on it until the future fires.
 *
 * This awaitable is the correct pattern for reactor-based systems:
 *   1. await_ready()   — fast path: if already done, don't suspend at all.
 *   2. await_suspend() — slow path: spawn a minimal detached thread that
 *                        blocks on fut.wait() (off-reactor), then posts the
 *                        coroutine handle back via Reactor::schedule().
 *                        The reactor thread is 100% free to serve other work.
 *   3. await_resume()  — extracts the value after wakeup.
 *
 * Cost: one detached thread per inference call. Acceptable because:
 *   - The thread blocks in the kernel (futex), consuming zero CPU.
 *   - Inference latency (~100ms) dwarfs thread spawn overhead (~5µs).
 *   - For a production system, this can be replaced with a thread-pool-based
 *     continuation mechanism (see comments at bottom of file).
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
        // Move the future into the thread so it outlives this awaitable.
        std::thread([h, r = reactor, f = std::move(fut), this]() mutable {
            result = f.get();  // Block only on this detached thread, never on the reactor.
            r->schedule(h);    // Wake the suspended coroutine via the reactor's external queue.
        }).detach();
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
