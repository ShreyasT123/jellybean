#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <type_traits>
#include <cstring>
#include <cstddef>
#include "jellybean/scheduler/fiber.hpp"
#include "jellybean/memory/arena.hpp"

namespace jellybean::actor {

/**
 * @brief Type-erased message with SSO (Small Stack Optimization).
 * 
 * Copy-safe with deep-copy semantics for heap-backed payloads.
 */
struct Message {
    static constexpr size_t INLINE_SIZE = 48;
    
    uint32_t type_id{0};
    uint32_t sender_shard{0};
    uint64_t sender_id{0};
    
    union {
        alignas(std::max_align_t) std::byte inline_data[INLINE_SIZE];
        struct { std::byte* ptr; size_t size; } heap_data;
    };
    bool is_inline{true};
    
    Message() {
        std::memset(inline_data, 0, INLINE_SIZE);
    }

    Message(const Message& other)
        : type_id(other.type_id),
          sender_shard(other.sender_shard),
          sender_id(other.sender_id),
          is_inline(other.is_inline) {
        if (is_inline) {
            std::memcpy(inline_data, other.inline_data, INLINE_SIZE);
        } else {
            heap_data.size = other.heap_data.size;
            heap_data.ptr = static_cast<std::byte*>(std::malloc(heap_data.size));
            if (!heap_data.ptr) std::abort();
            std::memcpy(heap_data.ptr, other.heap_data.ptr, heap_data.size);
        }
    }

    Message& operator=(const Message& other) {
        if (this == &other) return *this;
        destroy();
        type_id = other.type_id;
        sender_shard = other.sender_shard;
        sender_id = other.sender_id;
        is_inline = other.is_inline;
        if (is_inline) {
            std::memcpy(inline_data, other.inline_data, INLINE_SIZE);
        } else {
            heap_data.size = other.heap_data.size;
            heap_data.ptr = static_cast<std::byte*>(std::malloc(heap_data.size));
            if (!heap_data.ptr) std::abort();
            std::memcpy(heap_data.ptr, other.heap_data.ptr, heap_data.size);
        }
        return *this;
    }

    Message(Message&& other) noexcept 
        : type_id(other.type_id), 
          sender_shard(other.sender_shard), 
          sender_id(other.sender_id), 
          is_inline(other.is_inline) {
        if (is_inline) {
            std::memcpy(inline_data, other.inline_data, INLINE_SIZE);
        } else {
            heap_data = other.heap_data;
            other.heap_data.ptr = nullptr;
            other.heap_data.size = 0;
            other.is_inline = true; // Reset other to safe state
            std::memset(other.inline_data, 0, INLINE_SIZE);
        }
    }

    Message& operator=(Message&& other) noexcept {
        if (this != &other) {
            destroy();
            type_id = other.type_id;
            sender_shard = other.sender_shard;
            sender_id = other.sender_id;
            is_inline = other.is_inline;
            if (is_inline) {
                std::memcpy(inline_data, other.inline_data, INLINE_SIZE);
            } else {
                heap_data = other.heap_data;
                other.heap_data.ptr = nullptr;
                other.heap_data.size = 0;
                other.is_inline = true;
                std::memset(other.inline_data, 0, INLINE_SIZE);
            }
        }
        return *this;
    }

    ~Message() {
        destroy();
    }

    template<typename T>
    void set(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(alignof(T) <= alignof(std::max_align_t));
        destroy();
        if constexpr (sizeof(T) <= INLINE_SIZE) {
            std::memcpy(inline_data, &val, sizeof(T));
            is_inline = true;
        } else {
            heap_data.size = sizeof(T);
            heap_data.ptr = static_cast<std::byte*>(std::malloc(sizeof(T)));
            if (!heap_data.ptr) std::abort();
            std::memcpy(heap_data.ptr, &val, sizeof(T));
            is_inline = false;
        }
    }

    template<typename T>
    const T& as() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(alignof(T) <= alignof(std::max_align_t));
        if (is_inline) {
            if constexpr (sizeof(T) > INLINE_SIZE) {
                std::abort();
            }
            return *reinterpret_cast<const T*>(inline_data);
        }
        return *reinterpret_cast<const T*>(heap_data.ptr);
    }
    
    void destroy() {
        if (!is_inline && heap_data.ptr) {
            std::free(heap_data.ptr);
            heap_data.ptr = nullptr;
            heap_data.size = 0;
            is_inline = true;
        }
    }
};

/**
 * @brief Base class for all actors.
 */
class ActorBase {
public:
    using ActorId = uint64_t;
    
    virtual ~ActorBase() = default;
    
    /**
     * @brief Asynchronously receive a message.
     */
    virtual jellybean::scheduler::Task<> receive(Message msg) = 0;
    
    ActorId id() const noexcept { return id_; }
    uint32_t shard() const noexcept { return shard_id_; }

protected:
    ActorId id_;
    uint32_t shard_id_;
    jellybean::memory::ArenaAllocator* arena_;
};

} // namespace jellybean::actor
