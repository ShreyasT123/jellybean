#pragma once
#include <vector>
#include <functional>
#include <array>
#include <cstdint>

namespace jellybean::reactor {

/**
 * @brief Hashed timer wheel for efficient O(1) timer management.
 */
class TimerWheel {
public:
    static constexpr size_t SLOTS = 512;           // power of 2
    static constexpr uint64_t TICK_NS = 1000000;   // 1ms per tick

    using Callback = std::function<void()>;

    struct TimerEntry {
        uint64_t expires_tick;
        Callback callback;
    };

    TimerWheel();
    ~TimerWheel() = default;

    /**
     * @brief Initializes the starting tick.
     */
    void initialize(uint64_t now_ns);

    /**
     * @brief Adds a timer to the wheel.
     * @param delay_ns Delay from now in nanoseconds.
     * @param cb Callback to execute when timer expires.
     */
    void add_timer(uint64_t delay_ns, Callback cb);

    /**
     * @brief Advances the timer wheel and executes expired timers.
     * @param now_ns Current time in nanoseconds.
     */
    void advance(uint64_t now_ns);

private:
    std::array<std::vector<TimerEntry>, SLOTS> slots_;
    uint64_t current_tick_{0};
};

} // namespace jellybean::reactor
