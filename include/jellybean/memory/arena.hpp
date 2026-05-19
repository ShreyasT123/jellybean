#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

#include "jellybean/core/compiler.hpp"

namespace jellybean::memory {

/**
 * @brief High-performance Bump Pointer Arena Allocator.
 *
 * Designed for lock-free, single-threaded/shard-local allocation chains.
 * Extremely efficient for high-frequency serving loops where request-response
 * lifetimes dictate memory boundaries.
 *
 * Key Architecture Concepts:
 *
 *   1. BUMP ALLOCATION (O(1) complexity):
 *      Satisfies allocation requests by simply advancing a bump pointer (`current_`)
 *      by the size of the request (properly aligned). This bypasses complex search
 *      strategies (best-fit, buddy-system) and heap lock contentions entirely.
 *
 *   2. BULK RECYCLING (reset()):
 *      Individual destructions are bypassed. At request cycle boundaries, a single call
 *      to `reset()` immediately recycles all allocated blocks by resetting `current_`
 *      pointers back to the start of each block, providing zero-overhead garbage collection.
 *
 *   3. BLOCK CHAINING:
 *      Pre-allocates chunks (default 2MB blocks) dynamically. If a block overflows, a new block
 *      is chained as a linked list (`Block* next`), ensuring unlimited allocation scale.
 */
class ArenaAllocator {
   public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 2 * 1024 * 1024;  // 2MB blocks

    explicit ArenaAllocator(size_t block_size = DEFAULT_BLOCK_SIZE);
    ~ArenaAllocator();

    // Non-copyable, movable to prevent duplicate ownership of chained blocks
    ArenaAllocator(const ArenaAllocator&) = delete;
    auto operator=(const ArenaAllocator&) -> ArenaAllocator& = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept;
    auto operator=(ArenaAllocator&&) noexcept -> ArenaAllocator&;

    /**
     * @brief Constructs an object of type T inside the Arena memory space.
     *
     * @tparam T Type of the object to construct.
     * @tparam Args Constructor argument types.
     * @return T* Pointer to the constructed object.
     */
    template <typename T, typename... Args>
    [[nodiscard]] auto construct(Args&&... args) -> T* {
        static_assert(std::is_trivially_destructible_v<T>,
                      "ArenaAllocator::construct only supports trivially destructible types");
        void* ptr = allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);  // Placement new
    }

    /**
     * @brief Allocates an aligned block of memory from the arena.
     *
     * @param size Size in bytes.
     * @param alignment Alignment boundary (must be a power of 2).
     * @return void* Aligned pointer to the allocated memory.
     */
    [[nodiscard]] auto allocate(size_t size, size_t alignment = alignof(std::max_align_t)) -> void*;

    /**
     * @brief Resets all blocks back to an empty state, instantly recycling the memory.
     */
    void reset() noexcept;

    /**
     * @brief Returns total active bytes allocated to objects across all blocks.
     */
    [[nodiscard]] auto bytes_allocated() const noexcept -> size_t;

    /**
     * @brief Returns total wasted padding bytes across all blocks.
     */
    auto bytes_wasted() const noexcept -> size_t {
        return total_allocated_ - bytes_allocated();
    }

   private:
    struct Block {
        Block* next;
        size_t size;
        size_t used;
        alignas(std::max_align_t) std::byte data[1];  // Flexible array member
    };

    auto allocate_slow(size_t size, size_t alignment) -> void*;
    auto allocate_block(size_t min_size) -> Block*;

    std::byte* current_{nullptr};  // Current bump pointer
    std::byte* end_{nullptr};      // End of the current block
    Block* head_{nullptr};         // Tail of the list (most recent block)
    Block* first_block_{nullptr};  // Head of the list (oldest block)
    size_t block_size_;            // Default size for new blocks
    size_t total_allocated_{0};    // Grand total memory allocated from system
};

}  // namespace jellybean::memory
