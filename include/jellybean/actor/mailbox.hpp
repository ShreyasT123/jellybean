#pragma once
#include <cassert>
#include <memory>
#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/actor/actor.hpp"

namespace jellybean::actor {

/**
 * @brief Mailbox for an actor.
 * 
 * Uses an MpmcQueue to allow multiple senders (producers) and one receiver (consumer).
 * Although only one actor (the owner) consumes from the mailbox, multiple actors
 * from different shards might send messages to it.
 */
class Mailbox {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;

    Mailbox(size_t capacity = DEFAULT_CAPACITY) {
        queue_ptr_ = std::make_unique<jellybean::concurrency::MpmcQueue<Message, 1024>>();
        (void)capacity;
        // Current implementation supports only 1024. Fail fast on mismatch.
        assert(capacity == DEFAULT_CAPACITY);
    }

    ~Mailbox() {
        // Cleanup messages remaining in the queue
        while (auto msg = try_pop()) {
            msg->destroy();
        }
    }

    bool try_push(Message&& msg) {
        return queue_ptr_->try_push(std::move(msg));
    }

    std::optional<Message> try_pop() {
        return queue_ptr_->try_pop();
    }

private:
    // Fixed size for now to match MpmcQueue requirements
    std::unique_ptr<jellybean::concurrency::MpmcQueue<Message, 1024>> queue_ptr_;
};

} // namespace jellybean::actor
