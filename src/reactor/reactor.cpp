#include "jellybean/reactor/reactor.hpp"
#include "jellybean/scheduler/fiber.hpp"
#include "jellybean/concurrency/backoff.hpp"
#include <chrono>

namespace jellybean::reactor {

thread_local Reactor* Reactor::current_ = nullptr;

Reactor::Reactor(std::unique_ptr<EventBackend> backend)
    : backend_(std::move(backend)) {}

Reactor::~Reactor() {
    if (current_ == this) {
        current_ = nullptr;
    }
}

void Reactor::run() {
    running_.store(true, std::memory_order_release);
    thread_id_ = std::this_thread::get_id();
    current_ = this;
    
    auto now_init = std::chrono::steady_clock::now().time_since_epoch();
    timer_wheel_.initialize(std::chrono::duration_cast<std::chrono::nanoseconds>(now_init).count());
    
    while (running_.load(std::memory_order_acquire)) {
        // 0. Process external queue
        process_external_queue();

        // 1. Process local run queue (fibers)
        scratch_queue_.clear();
        scratch_queue_.swap(run_queue_);
        
        for (auto h : scratch_queue_) {
            if (h && !h.done()) {
                h.resume();
            }
        }
        
        // 2. Process timers
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        timer_wheel_.advance(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        
        // 3. Poll I/O
        const bool no_local = run_queue_.empty();
        const bool no_external = external_pending_.load(std::memory_order_acquire) == 0;
        int timeout = (no_local && no_external) ? 1 : 0;
        if (backend_) {
            backend_->poll(timeout);
        }
    }
}

void Reactor::stop() {
    running_.store(false, std::memory_order_release);
    if (backend_) {
        backend_->wakeup();
    }
}

void Reactor::schedule(std::coroutine_handle<> h) {
    if (std::this_thread::get_id() == thread_id_) {
        run_queue_.push_back(h);
    } else {
        concurrency::Backoff backoff;
        while (!external_queue_.try_push(h)) {
            backoff.pause();
        }
        external_pending_.fetch_add(1, std::memory_order_relaxed);
        if (backend_) {
            backend_->wakeup();
        }
    }
}

void Reactor::process_external_queue() {
    while (auto h = external_queue_.try_pop()) {
        run_queue_.push_back(*h);
        external_pending_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void Reactor::add_timer(uint64_t delay_ns, std::function<void()> cb) {
    timer_wheel_.add_timer(delay_ns, std::move(cb));
}

Reactor* Reactor::current() noexcept {
    return current_;
}

} // namespace jellybean::reactor

namespace jellybean::scheduler {
    using namespace jellybean::reactor;
    
    Reactor* current_reactor() {
        return Reactor::current();
    }

    void Yield::await_suspend(std::coroutine_handle<> h) noexcept {
        if (auto* r = current_reactor()) {
            r->schedule(h);
        }
    }
}
