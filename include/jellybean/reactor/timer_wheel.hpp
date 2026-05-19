#pragma once
#include <vector>
#include <functional>
#include <array>
#include <cstdint>

namespace jellybean::reactor {

/**
 * @brief Hashed circular Timer Wheel for high-performance timer management.
 * 
 * Traditional timer implementations utilize a priority queue (typically a heap)
 * which incurs $O(\log N)$ scheduling, cancellation, and tick advancement complexity.
 * When serving millions of active TCP connection timeouts, heap-based approaches
 * severely degrade under load.
 * 
 * Key Architecture Concepts:
 * 
 *   1. CIRCULAR SLOT BUCKETS ($O(1)$ complexity):
 *      We map future timers into discrete time slots arranged in a circular array of size 512.
 *      Scheduling a timer requires a simple bitwise index calculation: `expires_tick & (SLOTS - 1)`.
 *      Timers within the same slot are stored as an unordered list, providing guaranteed $O(1)$ insertions.
 * 
 *   2. HIERARCHICAL TIME RESOLUTION:
 *      Each slot represents a resolution tick of 1ms (`TICK_NS`). The wheel wraps around every 512ms.
 *      Timers that expire far in the future reside in the same bucket but carry their target absolute tick
 *      number (`expires_tick`). As the wheel advances, the tick advancement only fires timers whose target tick
 *      is <= the current clock tick.
 */
class TimerWheel {
public:
    static constexpr size_t SLOTS = 512;           // Number of slots in the circular wheel (must be power of 2)
    static constexpr uint64_t TICK_NS = 1000000;   // 1 millisecond tick duration in nanoseconds

    using Callback = std::function<void()>;

    struct TimerEntry {
        uint64_t expires_tick;                     // Target tick when this timer expires
        Callback callback;                         // Callback to execute on expiration
    };

    TimerWheel();
    ~TimerWheel() = default;

    /**
     * @brief Initializes the baseline starting tick.
     * 
     * @param now_ns Current absolute time in nanoseconds.
     */
    void initialize(uint64_t now_ns);

    /**
     * @brief Schedules a new timer callback on the wheel.
     * 
     * Calculates the slot bitwise and appends the callback.
     * 
     * @param delay_ns Nanosecond delay from the current time.
     * @param cb Callback function to run.
     */
    void add_timer(uint64_t delay_ns, Callback cb);

    /**
     * @brief Advances the clock tick and executes all expired timers.
     * 
     * Walks slots sequentially from the last checked tick to `now_ns`.
     * 
     * @param now_ns Current absolute time in nanoseconds.
     */
    void advance(uint64_t now_ns);

private:
    std::array<std::vector<TimerEntry>, SLOTS> slots_; // Fixed circular array of slot buckets
    uint64_t current_tick_{0};                         // Current absolute tick count of the wheel
};

} // namespace jellybean::reactor

