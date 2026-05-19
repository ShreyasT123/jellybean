#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace jellybean::reactor {

/**
 * @brief Global continuation thread pool for resolving std::futures.
 *
 * Instead of spawning a detached std::thread for every async wait,
 * FutureAwaitable posts a blocking task to this pool. The pool thread
 * waits on the future and posts the coroutine handle back to the reactor.
 * 
 * We use type erasure (std::function<void()>) so the pool can handle
 * futures of any type without being templated itself.
 */
class ContinuationPool {
   public:
    static auto instance() -> ContinuationPool&;

    ContinuationPool(const ContinuationPool&) = delete;
    auto operator=(const ContinuationPool&) -> ContinuationPool& = delete;

    /**
     * @brief Post a blocking wait task to the pool.
     */
    void post(std::function<void()> task);

   private:
    ContinuationPool();
    ~ContinuationPool();

    void worker_loop();

    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<std::function<void()>> queue_;
    std::atomic<bool> stopped_{false};
};

}  // namespace jellybean::reactor
