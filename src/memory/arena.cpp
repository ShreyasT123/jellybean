#include "jellybean/memory/arena.hpp"

#include <algorithm>
#include <cstdlib>
#include <cassert>

namespace jellybean::memory {

/**
 * @brief Constructor registers the default block size. Actual allocation is deferred until first
 * use.
 */
ArenaAllocator::ArenaAllocator(size_t block_size) : block_size_(block_size) {}

/**
 * @brief Destructor walks down the chained block list and frees all allocated blocks back to OS.
 */
ArenaAllocator::~ArenaAllocator() {
    Block* curr = first_block_;
    while (curr) {
        Block* next = curr->next;
        std::free(curr);  // Free the page block
        curr = next;
    }
}

/**
 * @brief Move constructor transfers block chain ownership smoothly.
 */
ArenaAllocator::ArenaAllocator(ArenaAllocator&& other) noexcept
    : current_(other.current_),
      end_(other.end_),
      head_(other.head_),
      first_block_(other.first_block_),
      block_size_(other.block_size_),
      total_allocated_(other.total_allocated_) {
    other.current_ = nullptr;
    other.end_ = nullptr;
    other.head_ = nullptr;
    other.first_block_ = nullptr;
    other.total_allocated_ = 0;
}

/**
 * @brief Move assignment operator ensures clean release of existing local resources.
 */
ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept {
    if (this != &other) {
        // Free existing block list
        Block* curr = first_block_;
        while (curr) {
            Block* next = curr->next;
            std::free(curr);
            curr = next;
        }

        // Steal new state
        current_ = other.current_;
        end_ = other.end_;
        head_ = other.head_;
        first_block_ = other.first_block_;
        block_size_ = other.block_size_;
        total_allocated_ = other.total_allocated_;

        other.current_ = nullptr;
        other.end_ = nullptr;
        other.head_ = nullptr;
        other.first_block_ = nullptr;
        other.total_allocated_ = 0;
    }
    return *this;
}

void* ArenaAllocator::allocate(size_t size, size_t alignment) {
    assert(alignment != 0);
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of 2");

    uintptr_t cur = reinterpret_cast<uintptr_t>(current_);

    // Fast-path bitwise pointer alignment
    uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);
    assert(aligned >= cur);

    uintptr_t next = aligned + size;
    assert(next >= aligned);

    // Fast path: if the aligned request fits within the current active block, return immediately!
    if (JELLYBEAN_LIKELY(next <= reinterpret_cast<uintptr_t>(end_))) {
        current_ = reinterpret_cast<std::byte*>(next);
        return reinterpret_cast<void*>(aligned);
    }

    // Slow path: allocate a new block and chain it
    return allocate_slow(size, alignment);
}

/**
 * @brief Slow-path allocation triggered when the requested size does not fit in the active block.
 */
void* ArenaAllocator::allocate_slow(size_t size, size_t alignment) {
    // Record current block's high-water mark before switching blocks
    if (head_) {
        head_->used = current_ - head_->data;
    }

    // Allocate a new block guaranteed to contain the aligned size
    size_t min_size = size + alignment;
    Block* block = allocate_block(min_size);

    uintptr_t cur = reinterpret_cast<uintptr_t>(block->data);
    uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);

    // Position bump pointer forward in the newly allocated page
    current_ = reinterpret_cast<std::byte*>(aligned + size);
    end_ = reinterpret_cast<std::byte*>(block->data + block->size);

    return reinterpret_cast<void*>(aligned);
}

/**
 * @brief Helper to request pages directly from the system malloc pool.
 */
ArenaAllocator::Block* ArenaAllocator::allocate_block(size_t min_size) {
    // Grab either our standard block size (2MB) or the requested size if it exceeds 2MB
    size_t size = std::max(block_size_, min_size);

    // Allocate contiguous storage: sizeof(Block struct) + size - 1 (due to data[1] padding field)
    void* ptr = std::malloc(sizeof(Block) + size - 1);
    if (!ptr) {
        std::abort();  // Severe OOM
    }

    Block* block = static_cast<Block*>(ptr);
    block->size = size;
    block->used = 0;
    block->next = nullptr;

    // Append to linked block chain
    if (!first_block_) {
        first_block_ = block;
    } else {
        head_->next = block;
    }
    head_ = block;

    total_allocated_ += size;
    return block;
}

/**
 * @brief Walks down the chain of blocks to calculate total bytes actively consumed by objects.
 */
size_t ArenaAllocator::bytes_allocated() const noexcept {
    size_t total = 0;
    Block* curr = first_block_;
    while (curr) {
        if (curr == head_) {
            total += (current_ - curr->data);  // Active bump block high-water offset
        } else {
            total += curr->used;  // Frozen filled blocks
        }
        curr = curr->next;
    }
    return total;
}

/**
 * @brief Instantly resets all page bump pointers back to empty, recycling memory for the next loop.
 */
void ArenaAllocator::reset() noexcept {
    if (!first_block_) return;

    // Clear all high-water limits
    Block* curr = first_block_;
    while (curr) {
        curr->used = 0;
        curr = curr->next;
    }

    // Reposition active page focus back to the oldest pre-allocated block
    head_ = first_block_;
    current_ = head_->data;
    end_ = head_->data + head_->size;
}

}  // namespace jellybean::memory
