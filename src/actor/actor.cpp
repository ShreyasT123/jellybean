#include "jellybean/actor/actor.hpp"
#include <cstdlib>
#include <cstring>

namespace jellybean::actor {

Message::Message() {
    std::memset(inline_data, 0, INLINE_SIZE);
}

Message::Message(const Message& other)
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

Message& Message::operator=(const Message& other) {
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

Message::Message(Message&& other) noexcept 
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
        other.is_inline = true; // Reset other to a safe inline state
        std::memset(other.inline_data, 0, INLINE_SIZE);
    }
}

Message& Message::operator=(Message&& other) noexcept {
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

Message::~Message() {
    destroy();
}

void Message::destroy() {
    if (!is_inline && heap_data.ptr) {
        std::free(heap_data.ptr);
        heap_data.ptr = nullptr;
        heap_data.size = 0;
        is_inline = true;
    }
}

} // namespace jellybean::actor
