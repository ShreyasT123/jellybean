#pragma once
#include <cstdint>
#include <thread>
#include "jellybean/core/compiler.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace jellybean::concurrency {

/**
 * @brief Exponential backoff for lock-free spinning.
 * 
 * Prevents excessive CPU consumption and cache thrashing during contention.
 */
class Backoff {
public:
    void pause() noexcept {
        if (count_ < 10) {
            // Spin
#if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
#elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
#endif
        } else if (count_ < 20) {
            // Yield
            std::this_thread::yield();
        } else {
            // Sleep
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        count_++;
    }

    void reset() noexcept {
        count_ = 0;
    }

private:
    uint32_t count_{0};
};

} // namespace jellybean::concurrency
