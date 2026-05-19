#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include <type_traits>
#include <utility>
#include "jellybean/core/compiler.hpp"

namespace jellybean::concurrency {

/**
 * @brief Multi-Producer Multi-Consumer (MPMC) lock-free bounded queue.
 * 
 * Based on Dmitry Vyukov's highly-optimized array-based bounded MPMC queue.
 * Extremely efficient for thread pools and work distribution engines.
 * 
 * Key Design Mechanics:
 * 
 *   1. SEQUENCE-COUNTER SYNCHRONIZATION:
 *      Each slot in the array contains a data storage field AND a `sequence` counter.
 *      The sequence counter dictates who is allowed to access the slot:
 *        - An enqueue operation is allowed at slot `i` if `sequence == pos`.
 *        - A dequeue operation is allowed at slot `i` if `sequence == pos + 1`.
 *      This elegant mechanism eliminates the traditional ABA problem and cache thrashing.
 * 
 *   2. CONCURRENT TICKETS (compare_exchange_weak):
 *      Multiple producers and consumers compete to claim tickets by atomic increments of
 *      `enqueue_pos_` and `dequeue_pos_`. Once a thread claims a ticket, it performs
 *      local operations exclusively in its assigned slot, avoiding global locks.
 * 
 *   3. FALSE SHARING AVOIDANCE:
 *      Important control variables (`slots_`, `enqueue_pos_`, and `dequeue_pos_`) are
 *      padded and aligned using `alignas(64)` to sit on separate cache lines.
 * 
 * @tparam T The element type stored in the queue.
 * @tparam Capacity Bounded size of the queue (must be >= 2 and a power of 2).
 */
template<typename T, size_t Capacity>
    requires (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0)
class MpmcQueue {
public:
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    /**
     * @brief Constructor initializes each slot's sequence number to its corresponding index.
     * This establishes the first "turn" for enqueuing at that slot.
     */
    MpmcQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Destructor drains the queue to ensure all elements are properly destroyed.
     */
    ~MpmcQueue() {
        while (try_pop()) {}
    }

    // Non-copyable
    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    /**
     * @brief Pushes an item to the queue by copying. Called by any producer thread.
     */
    [[nodiscard]] bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return emplace(item);
    }

    /**
     * @brief Pushes an item to the queue by moving. Called by any producer thread.
     */
    [[nodiscard]] bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace(std::move(item));
    }

    /**
     * @brief Constructs an item in-place directly in the ring buffer. Called by any producer.
     */
    template<typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Slot& slot = slots_[pos & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                // The slot is ready for enqueuing at this sequence turn.
                // Attempt to atomically claim this ticket by incrementing enqueue_pos_.
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    // Ticket successfully claimed! Construct object directly inside aligned slot storage
                    new (&slot.storage) T(std::forward<Args>(args)...);
                    
                    // Mark slot sequence as pos + 1 with release barrier.
                    // This signals to consumer threads that the data has been written and is ready to pop.
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // The slot's sequence is behind, meaning the queue is full.
                return false;
            } else {
                // Another thread beat us and claimed this slot, reload current position and retry.
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Pops and moves an item out of the queue. Called by any consumer.
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Slot& slot = slots_[pos & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                // The slot is ready for dequeuing.
                // Attempt to atomically claim this ticket by incrementing dequeue_pos_.
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    // Ticket successfully claimed! Extract and move the object
                    T* ptr = reinterpret_cast<T*>(&slot.storage);
                    T item = std::move(*ptr);
                    
                    // Manually trigger the destructor since we used placement new
                    ptr->~T();
                    
                    // Update slot sequence to pos + MASK + 1 with release barrier.
                    // This hands ownership back to the producer threads for the next turn.
                    slot.sequence.store(pos + MASK + 1, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                // The slot's sequence is behind, meaning the queue is empty.
                return std::nullopt;
            } else {
                // Another thread beat us and popped this slot, reload current position and retry.
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct alignas(CACHE_LINE) Slot {
        std::atomic<size_t> sequence;
        alignas(T) std::byte storage[sizeof(T)];
    };

    // Separate active variables onto unique 64-byte boundaries to completely prevent false-sharing L1/L2 cache bouncing
    alignas(CACHE_LINE) std::array<Slot, Capacity> slots_;
    alignas(CACHE_LINE) std::atomic<size_t> enqueue_pos_{0};
    alignas(CACHE_LINE) std::atomic<size_t> dequeue_pos_{0};
};

} // namespace jellybean::concurrency
