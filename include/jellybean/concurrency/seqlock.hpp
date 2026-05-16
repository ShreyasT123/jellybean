#pragma once
#include <atomic>
#include <cstdint>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include "jellybean/core/compiler.hpp"

namespace jellybean::concurrency {

/**
 * @brief Sequence lock for read-heavy shared data.
 * 
 * Writers increment a version counter. Readers check the counter before and after
 * reading. If the counter changes or is odd, the read is retried.
 * Provides no-lock, no-atomic-RMW read path.
 */
class Seqlock {
public:
    Seqlock() = default;
    ~Seqlock() = default;

    // Non-copyable
    Seqlock(const Seqlock&) = delete;
    Seqlock& operator=(const Seqlock&) = delete;

    template<typename F>
    void write(F&& fn) {
        while (true) {
            uint64_t s = seq_.load(std::memory_order_relaxed);
            if (JELLYBEAN_UNLIKELY(s & 1)) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#endif
                continue;
            }
            if (seq_.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                std::atomic_signal_fence(std::memory_order_acq_rel);
                
                fn();
                
                std::atomic_signal_fence(std::memory_order_acq_rel);
                seq_.store(s + 2, std::memory_order_release);
                return;
            }
        }
    }

    template<typename T, typename F>
    T read(F&& fn) const {
        while (true) {
            uint64_t s = seq_.load(std::memory_order_acquire);
            if (JELLYBEAN_UNLIKELY(s & 1)) {
                // Writer active, spin
                continue;
            }
            
            T result = fn();
            
            std::atomic_thread_fence(std::memory_order_acquire);
            if (JELLYBEAN_LIKELY(seq_.load(std::memory_order_relaxed) == s)) {
                return result;
            }
            // seq changed during read — retry
        }
    }

private:
    std::atomic<uint64_t> seq_{0};
};

} // namespace jellybean::concurrency
