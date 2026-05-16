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
 * @brief Single-Producer Single-Consumer lock-free queue.
 * 
 * Inspired by Aeron and Seastar. Uses power-of-2 capacity for fast modulo.
 * Uses separate cache lines for head and tail to avoid false sharing.
 */
template<typename T, size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)
class SpscQueue {
public:
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    SpscQueue() = default;
    ~SpscQueue() {
        while (try_pop()) {}
    }

    // Non-copyable, non-movable (due to fixed buffer and alignment)
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    /**
     * @brief Pushes an item to the queue. Called by producer ONLY.
     */
    [[nodiscard]] bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return emplace(item);
    }

    [[nodiscard]] bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace(std::move(item));
    }

    template<typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t next = t + 1;
        
        // Check if full — only read head with acquire if we think we're full
        if (next - head_.load(std::memory_order_acquire) > Capacity) {
            return false;
        }
        
        new (&buffer_[t & MASK].storage) T(std::forward<Args>(args)...);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the queue. Called by consumer ONLY.
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        
        T* ptr = reinterpret_cast<T*>(&buffer_[h & MASK].storage);
        T item = std::move(*ptr);
        ptr->~T();
        head_.store(h + 1, std::memory_order_release);
        return item;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) -
               head_.load(std::memory_order_acquire);
    }

private:
    alignas(CACHE_LINE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE) std::atomic<size_t> tail_{0};
    
    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
    };
    alignas(CACHE_LINE) std::array<Slot, Capacity> buffer_;
};

} // namespace jellybean::concurrency
