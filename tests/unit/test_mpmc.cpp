#include <gtest/gtest.h>
#include "jellybean/concurrency/mpmc_queue.hpp"
#include <thread>
#include <vector>
#include <set>
#include <mutex>

using namespace jellybean::concurrency;

TEST(MpmcQueueTest, BasicPushPop) {
    MpmcQueue<int, 4> q;
    
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_TRUE(q.try_push(4));
    EXPECT_FALSE(q.try_push(5));
    
    EXPECT_EQ(q.try_pop(), 1);
    EXPECT_EQ(q.try_pop(), 2);
    EXPECT_EQ(q.try_pop(), 3);
    EXPECT_EQ(q.try_pop(), 4);
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(MpmcQueueTest, MultiThreaded) {
    MpmcQueue<int, 1024> q;
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 10000;
    const int total_items = num_producers * items_per_producer;
    
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                int val = p * items_per_producer + i;
                while (!q.try_push(std::move(val))) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    std::vector<int> results;
    std::mutex results_mutex;
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&]() {
            for (int i = 0; i < total_items / num_consumers; ++i) {
                std::optional<int> val;
                while (!(val = q.try_pop())) {
                    std::this_thread::yield();
                }
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(*val);
            }
        });
    }
    
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(results.size(), total_items);
    std::set<int> unique_results(results.begin(), results.end());
    EXPECT_EQ(unique_results.size(), total_items);
}
