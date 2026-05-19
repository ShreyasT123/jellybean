#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

#include "jellybean/core/compiler.hpp"

namespace jellybean::concurrency {

/**
 * @brief Single-Producer Single-Consumer (SPSC) lock-free ring buffer queue.
 *
 * Inspired by Aeron and Seastar core-affinity message queues.
 *
 * Key Low-Latency Design Patterns:
 *
 *   1. LOCK-FREE SYNCHRONIZATION:
 *      Uses explicit atomic acquire-release memory ordering barriers instead of locking mutexes.
 *      The producer thread only modifies the `tail_` index, while the consumer thread only
 *      modifies the `head_` index.
 *
 *   2. FALSE SHARING ELIMINATION (alignas(64)):
 *      Modern multi-core CPU architectures cache memory in 64-byte chunks (cache lines).
 *      If a reader writes to `head_` and a writer writes to `tail_` on the same cache line,
 *      their L1/L2 caches constantly invalidate each other, causing massive bus contention.
 *      We force both `head_` and `tail_` onto distinct 64-byte aligned boundaries to eliminate
 * this.
 *
 *   3. FAST INDEXING (Capacity & (Capacity - 1) == 0):
 *      We require the queue capacity to be a power of two. This allows using bitwise AND (`& MASK`)
 *      instead of the expensive modulo division operator (`%`) which takes multiple CPU cycles.
 *
 * @tparam T The element type stored in the queue.
 * @tparam Capacity Bounded size of the queue (must be a power of 2).
 */
template <typename T, size_t Capacity>
    requires(Capacity > 0 && (Capacity & (Capacity - 1)) == 0)
class SpscQueue {
   public:
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    SpscQueue() = default;

    /**
     * @brief Destructor drains the queue to ensure all elements are properly destroyed.
     */
    ~SpscQueue() {
        while (try_pop()) {
        }
    }

    // Non-copyable, non-movable due to thread pinning, cache-alignment, and inline fixed buffer.
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    /**
     * @brief Pushes an item to the queue by copying. Called by producer thread ONLY.
     * @param item The element to copy-enqueue.
     * @return true if successful, false if the queue is full.
     */
    [[nodiscard]] bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return emplace(item);
    }

    /**
     * @brief Pushes an item to the queue by moving. Called by producer thread ONLY.
     * @param item The element to move-enqueue.
     * @return true if successful, false if the queue is full.
     */
    [[nodiscard]] bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace(std::move(item));
    }

    /**
     * @brief Constructs an item in-place directly in the ring buffer. Called by producer thread
     * ONLY.
     *
     * @tparam Args Variadic template arguments forwarded to T's constructor.
     * @return true if successful, false if the queue is full.
     */
    template <typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args&&...>) {
        // Read tail relaxed since only the producer thread writes to it
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t next = t + 1;

        // Optimize full check: load consumer head with acquire barrier to synchronize with pop()'s
        // release
        const size_t current_head = head_.load(std::memory_order_acquire);
        if (next - current_head > Capacity) {
            return false;  // Queue is completely full
        }

        // Construct the object in-place directly inside the pre-allocated byte storage (placement
        // new)
        new (&buffer_[t & MASK].storage) T(std::forward<Args>(args)...);

        // Store incremented tail index with release barrier.
        // This ensures the compiler and CPU do not reorder the object constructor *after* the index
        // increment, so the consumer thread will only see the updated index when the data is fully
        // written.
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops and moves an item out of the queue. Called by consumer thread ONLY.
     *
     * @return std::optional<T> The popped element, or std::nullopt if empty.
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        // Read head relaxed since only the consumer thread modifies it
        const size_t h = head_.load(std::memory_order_relaxed);

        // Load producer tail index with acquire barrier to ensure we read the latest writes from
        // emplace()
        if (h == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Queue is empty
        }

        // Retrieve and move the object from placement storage
        T* ptr = reinterpret_cast<T*>(&buffer_[h & MASK].storage);
        T item = std::move(*ptr);

        // Manually trigger the destructor since we used placement new
        ptr->~T();

        // Store incremented head index with release barrier.
        // This notifies the producer that the slot is now free and recycled.
        head_.store(h + 1, std::memory_order_release);
        return item;
    }

    /**
     * @brief Checks if the queue is currently empty.
     */
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Returns the current number of elements in the queue.
     */
    [[nodiscard]] size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

   private:
    // Pinned to separate cache lines (64 bytes) to avoid false-sharing thrashing between
    // producer/consumer cores
    alignas(CACHE_LINE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE) std::atomic<size_t> tail_{0};

    // Aligned storage to avoid any unaligned memory accesses on physical CPU caches
    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
    };

    // The underlying circular buffer array, aligned to a cacheline boundary
    alignas(CACHE_LINE) std::array<Slot, Capacity> buffer_;
};

}  // namespace jellybean::concurrency
