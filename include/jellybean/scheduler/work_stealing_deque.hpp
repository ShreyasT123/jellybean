#pragma once
#include <atomic>
#include <optional>
#include <vector>
#include <cstdint>
#include <cassert>
#include "jellybean/core/compiler.hpp"

namespace jellybean::scheduler {

/**
 * @brief Chase-Lev dynamic circular work-stealing deque.
 * 
 * High-performance deque where the owner thread pushes and pops from one end (bottom),
 * and other threads steal from the other end (top).
 */
template<typename T>
class WorkStealingDeque {
public:
    WorkStealingDeque(int64_t initial_capacity = 1024) {
        auto* array = new Array(initial_capacity);
        array_.store(array, std::memory_order_relaxed);
    }

    ~WorkStealingDeque() {
        delete array_.load(std::memory_order_relaxed);
    }

    // Non-copyable
    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    /**
     * @brief Pushes an item to the bottom. Owner thread only.
     */
    void push(T item) {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_acquire);
        Array* a = array_.load(std::memory_order_relaxed);
        
        if (b - t > a->capacity - 1) {
            // Resize (rare, simplified here)
            // In a real implementation, we'd double the capacity
            a = resize(a, b, t);
        }
        
        a->store(b, item);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    /**
     * @brief Pops an item from the bottom. Owner thread only.
     */
    std::optional<T> pop() {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        Array* a = array_.load(std::memory_order_relaxed);
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);
        
        std::optional<T> item;
        if (t <= b) {
            item = a->load(b);
            if (t == b) {
                // Last item, competition with steal()
                if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    item = std::nullopt;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
        } else {
            bottom_.store(b + 1, std::memory_order_relaxed);
        }
        return item;
    }

    /**
     * @brief Steals an item from the top. Any thread.
     */
    std::optional<T> steal() {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom_.load(std::memory_order_acquire);
        
        if (t < b) {
            Array* a = array_.load(std::memory_order_consume);
            T item = a->load(t);
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return std::nullopt;
            }
            return item;
        } else {
            return std::nullopt;
        }
    }

private:
    struct Array {
        int64_t capacity;
        T* storage;
        
        Array(int64_t cap) : capacity(cap) {
            assert(capacity > 0);
            assert((capacity & (capacity - 1)) == 0 && "WorkStealingDeque capacity must be power-of-two");
            storage = new T[cap];
        }
        ~Array() {
            delete[] storage;
        }
        
        int64_t mask() const { return capacity - 1; }
        
        T load(int64_t i) {
            return storage[i & mask()];
        }
        
        void store(int64_t i, T val) {
            storage[i & mask()] = val;
        }
    };

    Array* resize(Array* old_array, int64_t b, int64_t t) {
        Array* new_array = new Array(old_array->capacity * 2);
        for (int64_t i = t; i < b; ++i) {
            new_array->store(i, old_array->load(i));
        }
        array_.store(new_array, std::memory_order_relaxed);
        // We'd ideally defer deleting old_array, but for simplicity:
        // delete old_array; 
        // Actually, deleting old_array here is dangerous because of steal()
        // In a real system, use hazard pointers or epoch reclamation.
        return new_array;
    }

    alignas(64) std::atomic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    alignas(64) std::atomic<Array*> array_{nullptr};
};

} // namespace jellybean::scheduler
