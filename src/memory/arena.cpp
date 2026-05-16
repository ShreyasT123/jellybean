#include "jellybean/memory/arena.hpp"
#include <algorithm>
#include <cstdlib>

namespace jellybean::memory {

ArenaAllocator::ArenaAllocator(size_t block_size)
    : block_size_(block_size) {}

ArenaAllocator::~ArenaAllocator() {
    Block* curr = first_block_;
    while (curr) {
        Block* next = curr->next;
        std::free(curr);
        curr = next;
    }
}

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

ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept {
    if (this != &other) {
        Block* curr = first_block_;
        while (curr) {
            Block* next = curr->next;
            std::free(curr);
            curr = next;
        }

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

void* ArenaAllocator::allocate_slow(size_t size, size_t alignment) {
    if (head_) {
        head_->used = current_ - head_->data;
    }

    size_t min_size = size + alignment;
    Block* block = allocate_block(min_size);
    
    uintptr_t cur = reinterpret_cast<uintptr_t>(block->data);
    uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);
    
    current_ = reinterpret_cast<std::byte*>(aligned + size);
    end_ = reinterpret_cast<std::byte*>(block->data + block->size);
    
    // total_used_ should only track actual usage, not entire block sizes
    // But since we just reset current_ to start of new block:
    // we need to be careful how we increment total_used_
    // A better way: calculate total_used_ on demand or update it on every allocate()
    
    return reinterpret_cast<void*>(aligned);
}

ArenaAllocator::Block* ArenaAllocator::allocate_block(size_t min_size) {
    size_t size = std::max(block_size_, min_size);
    void* ptr = std::malloc(sizeof(Block) + size - 1);
    if (!ptr) {
        std::abort();
    }
    
    Block* block = static_cast<Block*>(ptr);
    block->size = size;
    block->used = 0;
    block->next = nullptr;
    
    if (!first_block_) {
        first_block_ = block;
    } else {
        head_->next = block;
    }
    head_ = block;
    
    total_allocated_ += size;
    return block;
}

size_t ArenaAllocator::bytes_allocated() const noexcept {
    size_t total = 0;
    Block* curr = first_block_;
    while (curr) {
        if (curr == head_) {
            total += (current_ - curr->data);
        } else {
            total += curr->used;
        }
        curr = curr->next;
    }
    return total;
}

void ArenaAllocator::reset() noexcept {
    if (!first_block_) return;
    
    Block* curr = first_block_;
    while (curr) {
        curr->used = 0;
        curr = curr->next;
    }
    
    head_ = first_block_;
    current_ = head_->data;
    end_ = head_->data + head_->size;
}

} // namespace jellybean::memory
