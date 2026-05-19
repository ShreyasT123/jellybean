#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

#include "jellybean/core/compiler.hpp"

namespace jellybean::scheduler {

/**
 * @brief Chase-Lev dynamic circular lock-free work-stealing Deque.
 *
 * A state-of-the-art concurrent double-ended queue tailored for high-throughput
 * task schedulers (resembling Tokio's and Go's runtime engines).
 *
 * Key Mechanical Design Patterns:
 *
 *   1. OWNER VS. STEALER ASYMMETRY:
 *      - The OWNER thread (the local CPU reactor thread) interacts in a LIFO (Last-In-First-Out)
 *        manner at the bottom of the deque (`push()` and `pop()`). LIFO maximizes CPU cache
 * locality, re-running the most recently touched tasks immediately.
 *      - STEALER threads (idle worker threads looking for work) steal tasks in a FIFO
 *        (First-In-First-Out) manner from the top of the deque (`steal()`). FIFO steals the oldest
 *        tasks, which represent the largest sub-trees of work, balancing CPU loads efficiently.
 *
 *   2. ATOMIC SYNCHRONIZATION AND RACES:
 *      - Owner pushes and pops locally without locks. Atomic index boundaries protect safety.
 *      - When a single item remains in the deque, both the owner's `pop()` and a stealer's
 * `steal()` may compete to grab it. We resolve this hot race condition cleanly using an atomic CAS
 *        (Compare-and-Swap) operation on the `top_` index.
 *
 *   3. DYNAMIC CAPACITY RESIZING:
 *      Automatically doubles underlying circular buffer memory on overflow, copying active pointers
 *      smoothly and updating references using atomic array stores.
 *
 * @tparam T The element type (typically a task pointer or function callback).
 */
template <typename T>
class WorkStealingDeque {
   public:
    /**
     * @brief Constructor allocates the initial power-of-two capacity array.
     */
    WorkStealingDeque(int64_t initial_capacity = 1024) {
        auto* array = new Array(initial_capacity);
        array_.store(array, std::memory_order_relaxed);
    }

    /**
     * @brief Destructor deallocates the underlying array storage.
     */
    ~WorkStealingDeque() {
        for (Array* old : garbage_) {
            delete old;
        }
        delete array_.load(std::memory_order_relaxed);
    }

    // Non-copyable
    WorkStealingDeque(const WorkStealingDeque&) = delete;
    auto operator=(const WorkStealingDeque&) -> WorkStealingDeque& = delete;

    /**
     * @brief Pushes an item onto the bottom of the deque. Called by owner thread ONLY.
     *
     * @param item The element to push.
     */
    void push(T item) {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_acquire);
        Array* a = array_.load(std::memory_order_relaxed);

        // If the deque is full (size exceeds capacity), trigger dynamic resizing
        if (b - t > a->capacity - 1) {
            a = resize(a, b, t);
        }

        a->store(b, item);

        // Use a release memory fence to guarantee that the data store is fully visible
        // to other threads *before* the bottom index is atomically incremented.
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    /**
     * @brief Pops an item from the bottom of the deque. Called by owner thread ONLY.
     *
     * @return std::optional<T> The popped element (LIFO order), or std::nullopt if empty.
     */
    auto pop() -> std::optional<T> {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        Array* a = array_.load(std::memory_order_relaxed);
        bottom_.store(b, std::memory_order_relaxed);

        // Impose a sequential-consistency barrier to ensure that our decrement of bottom_ is
        // visible to stealers before we read the top_ index. This is critical to prevent
        // popping a stolen task!
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);

        std::optional<T> item;
        if (t <= b) {
            item = a->load(b);
            if (t == b) {
                // There is exactly one task left in the deque. Compete with concurrent steal()
                // threads. We attempt to claim it by atomically incrementing top_.
                if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                                  std::memory_order_relaxed)) {
                    item = std::nullopt;  // Stealer won the race and stole it!
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
        } else {
            // Deque was already empty
            bottom_.store(b + 1, std::memory_order_relaxed);
        }
        return item;
    }

    /**
     * @brief Steals an item from the top of the deque. Called by any other thread.
     *
     * @return std::optional<T> The stolen element (FIFO order), or std::nullopt if none found.
     */
    auto steal() -> std::optional<T> {
        // Load top_ with acquire semantics to synchronize with pop()/push()'s releases
        int64_t t = top_.load(std::memory_order_acquire);

        // Ensure ordering before loading bottom_
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            // Deque is not empty; read array pointer with consume semantics to guarantee data
            // dependency ordering
            Array* a = array_.load(std::memory_order_acquire);
            T item = a->load(t);

            // Atomically increment top_. If it fails, another stealer (or the owner) popped it.
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                              std::memory_order_relaxed)) {
                return std::nullopt;
            }
            return item;
        } else {
            return std::nullopt;  // Nothing to steal
        }
    }

   private:
    // Aligned wrapper managing the circular storage buffer
    struct Array {
        int64_t capacity;
        T* storage;

        Array(int64_t cap) : capacity(cap) {
            assert(capacity > 0);
            assert((capacity & (capacity - 1)) == 0 &&
                   "WorkStealingDeque capacity must be power-of-two");
            storage = new T[cap];
        }
        ~Array() {
            delete[] storage;
        }

        auto mask() const -> int64_t {
            return capacity - 1;
        }

        auto load(int64_t i) -> T {
            return storage[i & mask()];
        }

        void store(int64_t i, T val) {
            storage[i & mask()] = val;
        }
    };

    /**
     * @brief Dynamic resizing method. Doubles the capacity of the queue.
     */
    auto resize(Array* old_array, int64_t b, int64_t t) -> Array* {
        auto* new_array = new Array(old_array->capacity * 2);
        for (int64_t i = t; i < b; ++i) {
            new_array->store(i, old_array->load(i));
        }
        array_.store(new_array, std::memory_order_release);
        garbage_.push_back(old_array);
        return new_array;
    }

    // Pad and align top_ and bottom_ indices to separate cache lines (64 bytes) to prevent
    // false-sharing cacheline thrashing
    alignas(64) std::atomic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    alignas(64) std::atomic<Array*> array_{nullptr};
    std::vector<Array*> garbage_{};
};

}  // namespace jellybean::scheduler
