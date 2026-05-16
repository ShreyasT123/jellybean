#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <limits>
#include <new>
#include <utility>
#include "jellybean/core/compiler.hpp"

namespace jellybean::memory {

/**
 * @brief High-performance arena allocator for thread-local usage.
 * 
 * Provides O(1) allocation through bump-pointer strategy.
 * Blocks are allocated on demand and freed only upon destruction.
 */
class ArenaAllocator {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 2 * 1024 * 1024; // 2MB

    explicit ArenaAllocator(size_t block_size = DEFAULT_BLOCK_SIZE);
    ~ArenaAllocator();

    // Non-copyable, movable
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept;
    ArenaAllocator& operator=(ArenaAllocator&&) noexcept;

    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    [[nodiscard]] JELLYBEAN_FORCE_INLINE
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        assert(alignment != 0);
        assert((alignment & (alignment - 1)) == 0);
        uintptr_t cur = reinterpret_cast<uintptr_t>(current_);
        uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);
        assert(aligned >= cur);
        uintptr_t next = aligned + size;
        assert(next >= aligned);

        if (JELLYBEAN_LIKELY(next <= reinterpret_cast<uintptr_t>(end_))) {
            current_ = reinterpret_cast<std::byte*>(next);
            return reinterpret_cast<void*>(aligned);
        }
        return allocate_slow(size, alignment);
    }

    void reset() noexcept;

    size_t bytes_allocated() const noexcept;
    size_t bytes_wasted() const noexcept { return total_allocated_ - bytes_allocated(); }

private:
    struct Block {
        Block* next;
        size_t size;
        size_t used;
        alignas(std::max_align_t) std::byte data[1];
    };

    void* allocate_slow(size_t size, size_t alignment);
    Block* allocate_block(size_t min_size);

    std::byte* current_{nullptr};
    std::byte* end_{nullptr};
    Block* head_{nullptr};         // Tail of the list (most recent)
    Block* first_block_{nullptr};  // Head of the list (oldest)
    size_t block_size_;
    size_t total_allocated_{0};
};

} // namespace jellybean::memory
