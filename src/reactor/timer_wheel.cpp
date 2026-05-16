#include "jellybean/reactor/timer_wheel.hpp"
#include <algorithm>

namespace jellybean::reactor {

TimerWheel::TimerWheel() {
    // current_tick_ is initialized to some reasonable baseline or 0
}

void TimerWheel::add_timer(uint64_t delay_ns, Callback cb) {
    uint64_t ticks = delay_ns / TICK_NS;
    if (ticks == 0) ticks = 1; // Minimum 1 tick delay
    
    uint64_t expires_tick = current_tick_ + ticks;
    size_t slot = expires_tick & (SLOTS - 1);
    
    slots_[slot].push_back({expires_tick, std::move(cb)});
}

void TimerWheel::initialize(uint64_t now_ns) {
    current_tick_ = now_ns / TICK_NS;
}

void TimerWheel::advance(uint64_t now_ns) {
    uint64_t target_tick = now_ns / TICK_NS;
    
    while (current_tick_ < target_tick) {
        current_tick_++;
        size_t slot = current_tick_ & (SLOTS - 1);
        auto& entries = slots_[slot];
        
        if (entries.empty()) continue;

        // Move expired timers to a temporary list to avoid O(n^2) erase
        std::vector<TimerEntry> expired;
        std::vector<TimerEntry> survivors;
        survivors.reserve(entries.size());

        for (auto& entry : entries) {
            if (entry.expires_tick <= current_tick_) {
                expired.push_back(std::move(entry));
            } else {
                survivors.push_back(std::move(entry));
            }
        }
        
        entries = std::move(survivors);

        for (auto& entry : expired) {
            entry.callback();
        }
    }
}

} // namespace jellybean::reactor
