#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <type_traits>
#include <cstddef>
#include <cstring>
#include "jellybean/scheduler/fiber.hpp"
#include "jellybean/memory/arena.hpp"

namespace jellybean::actor {

/**
 * @brief Type-erased Message container utilizing Small Stack Optimization (SSO).
 * 
 * In high-frequency actor serving systems, millions of small control signals
 * (such as heartbeats, model configurations, small request frames, or latency markers)
 * are passed continuously. Allocating heap memory for each individual message
 * is highly expensive.
 * 
 * Key Architecture Concepts:
 * 
 *   1. SMALL STACK OPTIMIZATION (SSO):
 *      We define a static inline size of 48 bytes. If the payload object is <= 48 bytes,
 *      it is copied directly into an inline byte storage array (`inline_data`) on the stack.
 *      This completely bypasses the memory allocator for common small message payloads!
 * 
 *   2. DEEP-COPY FALLBACK:
 *      If the payload size exceeds 48 bytes (e.g. large tensor input buffers), the message
 *      allocates heap storage (`heap_data`), performing full deep copies to guarantee safety
 *      across asynchronous execution boundaries.
 * 
 *   3. COPY & MOVE COMPATIBILITY:
 *      Ensures robust and safe copy/move construction and assignment semantics, managing the
 *      lifetime of the inline/heap-allocated union fields properly.
 */
struct Message {
    static constexpr size_t INLINE_SIZE = 48;
    
    uint32_t type_id{0};          // Unique identifier for the payload's type
    uint32_t sender_shard{0};     // The shard ID of the sending actor
    uint64_t sender_id{0};        // The unique actor ID of the sender
    
    union {
        alignas(std::max_align_t) std::byte inline_data[INLINE_SIZE];
        struct { std::byte* ptr; size_t size; } heap_data;
    };
    bool is_inline{true};         // Indicates whether the payload is stored inline or on the heap
    
    Message();
    Message(const Message& other);
    Message& operator=(const Message& other);
    Message(Message&& other) noexcept;
    Message& operator=(Message&& other) noexcept;
    ~Message();

    /**
     * @brief Stores an object inside the message. Uses SSO if the size fits.
     * 
     * @tparam T Trivially copyable payload type.
     * @param val The payload value to store.
     */
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

    /**
     * @brief Retrieves a read-only reference to the stored object.
     * 
     * @tparam T Trivially copyable payload type.
     * @return const T& Reference to the stored payload.
     */
    template<typename T>
    const T& as() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(alignof(T) <= alignof(std::max_align_t));
        if (is_inline) {
            return *reinterpret_cast<const T*>(inline_data);
        }
        return *reinterpret_cast<const T*>(heap_data.ptr);
    }
    
    void destroy();
};

/**
 * @brief Base class for all asynchronous actors.
 */
class ActorBase {
public:
    using ActorId = uint64_t;
    
    virtual ~ActorBase() = default;
    
    /**
     * @brief Asynchronously dispatches and handles an incoming message.
     */
    virtual jellybean::scheduler::Task<> receive(Message msg) = 0;
    
    ActorId id() const noexcept { return id_; }
    uint32_t shard() const noexcept { return shard_id_; }

protected:
    explicit ActorBase(ActorId id, uint32_t shard_id, jellybean::memory::ArenaAllocator* arena) noexcept
        : id_(id), shard_id_(shard_id), arena_(arena) {}

    ActorId id_{0};                                   // Unique actor identifier
    uint32_t shard_id_{0};                            // The pinned CPU shard/event loop this actor resides in
    jellybean::memory::ArenaAllocator* arena_{nullptr}; // Shard-local arena allocator for request memory
};

} // namespace jellybean::actor
