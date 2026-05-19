#pragma once
#include <atomic>
#include <cstdint>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include "jellybean/core/compiler.hpp"

namespace jellybean::concurrency {

/**
 * @brief Sequence Lock (Seqlock) for read-heavy lock-free shared state.
 * 
 * Inspired by Linux kernel sequence locking mechanisms. Excellent for routing
 * tables, calibration settings, and telemetry state.
 * 
 * Key Architecture Concepts:
 * 
 *   1. LOCK-FREE READERS:
 *      Unlike traditional reader-writer locks, readers never write to any memory location.
 *      They do not perform atomic Read-Modify-Write (RMW) operations. This eliminates the cacheline
 *      invalidation traffic that severely degrades multi-core scalability when thousands of readers
 *      query a hot configuration.
 * 
 *   2. STARVATION-FREE WRITERS:
 *      Writers always win immediately and never block behind readers. They atomically increment
 *      a 64-bit sequence counter from an even number to an odd number before modifying the data,
 *      and increment it back to an even number upon completion.
 * 
 *   3. VERIFIED RETRY LOGIC:
 *      Readers grab the sequence count before reading the data structure, execute their read logic,
 *      and double-check that the sequence counter did not change and remained even throughout their
 *      read duration. If a change or odd count is detected, it means a concurrent write occurred
 *      during the read span. The reader transparently retries in a loop.
 */
class Seqlock {
public:
    Seqlock() = default;
    ~Seqlock() = default;

    // Non-copyable
    Seqlock(const Seqlock&) = delete;
    Seqlock& operator=(const Seqlock&) = delete;

    /**
     * @brief Modifies the shared data inside the given callback thread-safely.
     * 
     * @tparam F Callback function type void().
     * @param fn Callback that performs the actual write operation on the underlying data.
     */
    template<typename F>
    void write(F&& fn) {
        while (true) {
            uint64_t s = seq_.load(std::memory_order_relaxed);
            
            // If the sequence counter is odd, a writer is currently modifying the data.
            if (JELLYBEAN_UNLIKELY(s & 1)) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause(); // Issue hardware spin-wait hint to optimize CPU pipeline under hot-loops
#endif
                continue;
            }
            
            // Attempt to transition sequence counter from s (even) to s + 1 (odd)
            if (seq_.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                std::atomic_thread_fence(std::memory_order_release);
                
                // Execute user writing logic
                fn();
                
                std::atomic_thread_fence(std::memory_order_release);
                
                // Increment sequence counter back to s + 2 (even) with a release barrier.
                // This publishes all writes and notifies readers that the data is consistent.
                seq_.store(s + 2, std::memory_order_release);
                return;
            }
        }
    }

    /**
     * @brief Safely reads the shared data without locks or atomic writes.
     * 
     * @tparam T The return type of the read operation.
     * @tparam F Callback function type T().
     * @param fn Callback that reads the shared data and returns a local copy.
     * @return T A consistent local copy of the shared data.
     */
    template<typename T, typename F>
    T read(F&& fn) const {
        while (true) {
            // Load sequence counter with acquire barrier to prevent reads from being reordered *before* this check
            uint64_t s = seq_.load(std::memory_order_acquire);
            if (JELLYBEAN_UNLIKELY(s & 1)) {
                // Writer is currently active; spin until even
                continue;
            }
            
            // Read and construct the local copy
            T result = fn();
            
            // Impose an acquire thread barrier to ensure all data reads are completed *before* checking the counter
            std::atomic_thread_fence(std::memory_order_acquire);
            
            // Check if the sequence counter changed during the reading window.
            // If the counter matches our initial snapshot `s`, the read is guaranteed consistent!
            if (JELLYBEAN_LIKELY(seq_.load(std::memory_order_relaxed) == s)) {
                return result;
            }
            // Sequence changed; a concurrent write occurred. Loop back and retry.
        }
    }

private:
    std::atomic<uint64_t> seq_{0};
};

} // namespace jellybean::concurrency
