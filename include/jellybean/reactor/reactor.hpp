#pragma once
#include <vector>
#include <coroutine>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "jellybean/reactor/timer_wheel.hpp"
#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/concurrency/spsc_queue.hpp"

namespace jellybean::reactor {

/**
 * @brief Abstract base class for I/O backends.
 */
class EventBackend {
public:
    virtual ~EventBackend() = default;
    virtual int poll(int timeout_ms) = 0;
    virtual void wakeup() {} // Interrupt poll for external schedule
};

/**
 * @brief Per-core event loop and scheduler.
 */
class Reactor {
public:
    Reactor(std::unique_ptr<EventBackend> backend);
    ~Reactor();

    // Non-copyable
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    /**
     * @brief Starts the reactor loop.
     */
    void run();

    /**
     * @brief Stops the reactor loop. Thread-safe.
     */
    void stop();

    /**
     * @brief Schedules a coroutine. Local: fast path. External: thread-safe.
     */
    void schedule(std::coroutine_handle<> h);

    /**
     * @brief Adds a timer. Local only.
     */
    void add_timer(uint64_t delay_ns, std::function<void()> cb);

    [[nodiscard]] static Reactor* current() noexcept;

private:
    void process_external_queue();

    std::unique_ptr<EventBackend> backend_;
    TimerWheel timer_wheel_;
    std::vector<std::coroutine_handle<>> run_queue_;
    std::vector<std::coroutine_handle<>> scratch_queue_;
    
    // Thread-safe ingress for external threads
    jellybean::concurrency::MpmcQueue<std::coroutine_handle<>, 1024> external_queue_;
    std::atomic<size_t> external_pending_{0};
    
    std::atomic<bool> running_{false};
    std::thread::id thread_id_;
    
    static thread_local Reactor* current_;
};

} // namespace jellybean::reactor
