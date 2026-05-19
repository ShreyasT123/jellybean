#include "jellybean/reactor/timer_wheel.hpp"
#include <algorithm>

namespace jellybean::reactor {

/**
 * @brief Constructor.
 */
TimerWheel::TimerWheel() {
    // Baseline current_tick_ is initialized to 0
}

/**
 * @brief Schedules a new timer callback on the wheel.
 */
void TimerWheel::add_timer(uint64_t delay_ns, Callback cb) {
    // Map absolute nanosecond delay to tick count
    uint64_t ticks = delay_ns / TICK_NS;
    if (ticks == 0) ticks = 1; // Force a minimum 1 tick delay to guarantee async flow separation
    
    // Target tick absolute point
    uint64_t expires_tick = current_tick_ + ticks;
    
    // High-performance bitwise AND modulo slot calculation
    size_t slot = expires_tick & (SLOTS - 1);
    
    // Add timer to the corresponding bucket slot
    slots_[slot].push_back({expires_tick, std::move(cb)});
}

/**
 * @brief Initializes baseline absolute tick count.
 */
void TimerWheel::initialize(uint64_t now_ns) {
    current_tick_ = now_ns / TICK_NS;
}

/**
 * @brief Advances clock ticks sequentially up to target time and fires expired timers.
 */
void TimerWheel::advance(uint64_t now_ns) {
    uint64_t target_tick = now_ns / TICK_NS;
    
    // Step forward one tick at a time to check each slot sequentially, avoiding gaps
    while (current_tick_ < target_tick) {
        current_tick_++;
        
        size_t slot = current_tick_ & (SLOTS - 1);
        auto& entries = slots_[slot];
        
        if (entries.empty()) continue;

        auto mid = std::stable_partition(
            entries.begin(),
            entries.end(),
            [this](const TimerEntry& entry) { return entry.expires_tick > current_tick_; });

        for (auto it = mid; it != entries.end(); ++it) {
            auto& entry = *it;
            entry.callback();
        }
        entries.erase(mid, entries.end());
    }
}

} // namespace jellybean::reactor
