#include "jellybean/reactor/continuation_pool.hpp"

namespace jellybean::reactor {

auto ContinuationPool::instance() -> ContinuationPool& {
    static ContinuationPool pool;
    return pool;
}

ContinuationPool::ContinuationPool() {
    // 4 threads is enough since inference is the real bottleneck
    const int num_threads = 4;
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ContinuationPool::~ContinuationPool() {
    {
        std::lock_guard lock(mu_);
        stopped_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ContinuationPool::post(std::function<void()> task) {
    {
        std::lock_guard lock(mu_);
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void ContinuationPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
            if (stopped_ && queue_.empty()) {
                return;
            }
            task = std::move(queue_.front());
            queue_.erase(queue_.begin()); // not the most efficient queue, but fine for 4 threads
        }
        if (task) {
            task();
        }
    }
}

}  // namespace jellybean::reactor
