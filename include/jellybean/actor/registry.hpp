#pragma once
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include "jellybean/actor/actor.hpp"
#include "jellybean/actor/mailbox.hpp"

namespace jellybean::actor {

/**
 * @brief Thread-safe registry mapping ActorId to their corresponding Mailbox.
 * 
 * In a distributed actor model, actors are registered here on startup so that
 * other actors, schedulers, or network transport layers can lookup their mailboxes
 * to route and dispatch messages asynchronously.
 */
class ActorRegistry {
private:
    std::unordered_map<ActorBase::ActorId, std::shared_ptr<Mailbox>> registry_;
    mutable std::shared_mutex rw_mutex_; // Thread safety for high-throughput concurrent lookups

public:
    ActorRegistry() = default;
    
    // Non-copyable
    ActorRegistry(const ActorRegistry&) = delete;
    ActorRegistry& operator=(const ActorRegistry&) = delete;
    ActorRegistry(ActorRegistry&&) noexcept = delete;
    ActorRegistry& operator=(ActorRegistry&&) noexcept = delete;

    /**
     * @brief Registers an actor and its associated mailbox.
     * 
     * @param actor Pointer to the actor base class.
     * @param mailbox Shared pointer to the actor's lock-free mailbox.
     */
    void register_actor(ActorBase* actor, std::shared_ptr<Mailbox> mailbox) {
        if (!actor) return;
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        registry_[actor->id()] = std::move(mailbox);
    }

    /**
     * @brief Finds the mailbox associated with the given ActorId.
     * 
     * Returns nullptr if the actor is not registered.
     * Uses a shared read lock for optimal performance under multi-threaded lookup load.
     */
    std::shared_ptr<Mailbox> find_mailbox(ActorBase::ActorId id) const {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = registry_.find(id);
        if (it != registry_.end()) {
            return it->second;
        }
        return nullptr;
    }

    /**
     * @brief Unregisters an actor from the registry.
     */
    void unregister_actor(ActorBase::ActorId id) {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        registry_.erase(id);
    }
};

} // namespace jellybean::actor
