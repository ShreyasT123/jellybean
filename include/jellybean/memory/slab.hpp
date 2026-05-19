#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace jellybean::memory {

/**
 * @brief High-performance, O(1) Slab Allocator for fixed-size object pools.
 *
 * Inspired by the Linux kernel slab allocator and jemalloc's size classes.
 * It manages memory in contiguous chunks of ChunkObjects, utilizing an intrusive
 * free-list. When an object is free, its memory storage is reused as a pointer
 * to the next free slot, eliminating per-object allocation metadata overhead.
 *
 * @tparam T The type of object allocated by the slab.
 * @tparam ChunkObjects Number of objects allocated per slab chunk.
 */
template <typename T, size_t ChunkObjects = 64>
class SlabAllocator {
   private:
    // A Slot is either an active instance of T or a pointer to another free Slot.
    union Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot* next_free;
    };

    // A Chunk is a contiguous array of ChunkObjects Slots.
    struct Chunk {
        Slot slots[ChunkObjects];
    };

    std::vector<std::unique_ptr<Chunk>> chunks_;  // Owns the allocated chunks
    Slot* free_list_head_{nullptr};               // Head of the intrusive free list

    /**
     * @brief Allocates a new chunk and chains its slots to the free list.
     */
    void allocate_chunk() {
        auto chunk = std::make_unique<Chunk>();

        // Chain the newly allocated slots together in the free list (LIFO structure)
        for (size_t i = 0; i < ChunkObjects; ++i) {
            Slot* slot = &chunk->slots[i];
            slot->next_free = free_list_head_;
            free_list_head_ = slot;
        }
        chunks_.push_back(std::move(chunk));
    }

   public:
    SlabAllocator() = default;

    // Non-copyable, movable
    SlabAllocator(const SlabAllocator&) = delete;
    auto operator=(const SlabAllocator&) -> SlabAllocator& = delete;
    SlabAllocator(SlabAllocator&&) noexcept = default;
    auto operator=(SlabAllocator&&) noexcept -> SlabAllocator& = default;

    ~SlabAllocator() {
        // In typical slab allocators, destruction of individual elements is done
        // explicitly via deallocate. Chunks and their backing memory are freed automatically.
    }

    /**
     * @brief Allocates and in-place constructs an object of type T.
     *
     * If the free list is empty, a new Chunk is allocated to satisfy the request.
     * Complexity is O(1) amortized.
     */
    template <typename... Args>
    [[nodiscard]] auto allocate(Args&&... args) -> T* {
        if (!free_list_head_) {
            allocate_chunk();
        }
        Slot* slot = free_list_head_;
        free_list_head_ = slot->next_free;

        // In-place placement new construction of the object
        T* obj = new (slot->storage) T(std::forward<Args>(args)...);
        return obj;
    }

    /**
     * @brief Explicitly destructs and deallocates the given object, returning its slot to the free
     * list.
     *
     * Complexity is O(1) guaranteed.
     */
    void deallocate(T* ptr) {
        if (!ptr) return;

        // Explicitly invoke the destructor of type T
        ptr->~T();

        // Return the slot back to the head of the intrusive free list
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        slot->next_free = free_list_head_;
        free_list_head_ = slot;
    }
};

}  // namespace jellybean::memory
