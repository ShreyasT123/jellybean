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
 * @brief Multi-Producer Multi-Consumer lock-free queue.
 * 
 * Based on Dmitry Vyukov's bounded MPMC queue.
 * Each slot has a sequence number to prevent ABA and manage synchronization.
 */
template<typename T, size_t Capacity>
    requires (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0)
class MpmcQueue {
public:
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    MpmcQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpmcQueue() {
        while (try_pop()) {}
    }

    // Non-copyable
    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    [[nodiscard]] bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return emplace(item);
    }

    [[nodiscard]] bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace(std::move(item));
    }

    template<typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Slot& slot = slots_[pos & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    new (&slot.storage) T(std::forward<Args>(args)...);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // Full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Slot& slot = slots_[pos & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T* ptr = reinterpret_cast<T*>(&slot.storage);
                    T item = std::move(*ptr);
                    ptr->~T();
                    slot.sequence.store(pos + MASK + 1, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                return std::nullopt; // Empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Slot {
        std::atomic<size_t> sequence;
        alignas(T) std::byte storage[sizeof(T)];
    };

    alignas(CACHE_LINE) std::array<Slot, Capacity> slots_;
    alignas(CACHE_LINE) std::atomic<size_t> enqueue_pos_{0};
    alignas(CACHE_LINE) std::atomic<size_t> dequeue_pos_{0};
};

} // namespace jellybean::concurrency
