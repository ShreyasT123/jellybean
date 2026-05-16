#include <gtest/gtest.h>
#include "jellybean/concurrency/spsc_queue.hpp"
#include <thread>
#include <vector>

using namespace jellybean::concurrency;

TEST(SpscQueueTest, BasicPushPop) {
    SpscQueue<int, 4> q;
    
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);
    
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_TRUE(q.try_push(4));
    EXPECT_FALSE(q.try_push(5)); // Full
    
    EXPECT_EQ(q.size(), 4);
    
    EXPECT_EQ(q.try_pop(), 1);
    EXPECT_EQ(q.try_pop(), 2);
    EXPECT_EQ(q.try_pop(), 3);
    EXPECT_EQ(q.try_pop(), 4);
    EXPECT_EQ(q.try_pop(), std::nullopt); // Empty
}

TEST(SpscQueueTest, ProducerConsumerThreaded) {
    SpscQueue<int, 1024> q;
    const int count = 100000;
    
    std::thread producer([&]() {
        for (int i = 0; i < count; ++i) {
            int val = i;
            while (!q.try_push(std::move(val))) {
                std::this_thread::yield();
            }
        }
    });
    
    std::thread consumer([&]() {
        for (int i = 0; i < count; ++i) {
            std::optional<int> val;
            while (!(val = q.try_pop())) {
                std::this_thread::yield();
            }
            EXPECT_EQ(*val, i);
        }
    });
    
    producer.join();
    consumer.join();
}
