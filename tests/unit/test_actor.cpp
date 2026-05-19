#include <gtest/gtest.h>
#include "jellybean/actor/actor.hpp"
#include "jellybean/actor/mailbox.hpp"
#include "jellybean/actor/registry.hpp"

using namespace jellybean::actor;
using namespace jellybean::scheduler;

class TestActor : public ActorBase {
public:
    TestActor(ActorId id, uint32_t shard) : ActorBase(id, shard, nullptr) {}

    int last_received_val = 0;
    int received_count = 0;

    Task<> receive(Message msg) override {
        last_received_val = msg.as<int>();
        received_count++;
        co_return;
    }
};

TEST(ActorTest, BasicMessaging) {
    TestActor actor(1, 0);
    Mailbox mailbox;
    
    Message msg;
    msg.set(42);
    
    mailbox.try_push(std::move(msg));
    
    auto received = mailbox.try_pop();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->as<int>(), 42);
    
    // Test actor receive (manual drive)
    auto task = actor.receive(std::move(*received));
    task.handle.resume(); // Start coroutine
    
    EXPECT_EQ(actor.last_received_val, 42);
    EXPECT_EQ(actor.received_count, 1);
}

TEST(ActorRegistryTest, RegisterFind) {
    ActorRegistry registry;
    TestActor actor(1, 0);
    auto mailbox = std::make_shared<Mailbox>();
    
    registry.register_actor(&actor, mailbox);
    
    auto found = registry.find_mailbox(1);
    EXPECT_EQ(found, mailbox);
    
    registry.unregister_actor(1);
    EXPECT_EQ(registry.find_mailbox(1), nullptr);
}
