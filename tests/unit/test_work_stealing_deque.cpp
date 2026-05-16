#include <gtest/gtest.h>
#include "jellybean/scheduler/work_stealing_deque.hpp"
#include <thread>
#include <vector>
#include <set>
#include <mutex>

using namespace jellybean::scheduler;

TEST(WorkStealingDequeTest, BasicPushPop) {
    WorkStealingDeque<int> deque(4);
    
    deque.push(1);
    deque.push(2);
    deque.push(3);
    
    EXPECT_EQ(deque.pop(), 3);
    EXPECT_EQ(deque.pop(), 2);
    EXPECT_EQ(deque.pop(), 1);
    EXPECT_EQ(deque.pop(), std::nullopt);
}

TEST(WorkStealingDequeTest, Steal) {
    WorkStealingDeque<int> deque(1024);
    
    deque.push(1);
    deque.push(2);
    deque.push(3);
    
    EXPECT_EQ(deque.steal(), 1);
    EXPECT_EQ(deque.steal(), 2);
    EXPECT_EQ(deque.pop(), 3);
    EXPECT_EQ(deque.steal(), std::nullopt);
}

TEST(WorkStealingDequeTest, ConcurrentSteal) {
    WorkStealingDeque<int> deque(4096);
    const int count = 1000;
    
    for (int i = 0; i < count; ++i) {
        deque.push(i);
    }
    
    std::vector<int> stolen;
    std::mutex stolen_mutex;
    
    std::thread stealer([&]() {
        for (int i = 0; i < count / 2; ++i) {
            std::optional<int> val;
            while (!(val = deque.steal())) {
                std::this_thread::yield();
            }
            std::lock_guard<std::mutex> lock(stolen_mutex);
            stolen.push_back(*val);
        }
    });
    
    std::vector<int> popped;
    for (int i = 0; i < count / 2; ++i) {
        std::optional<int> val;
        while (!(val = deque.pop())) {
            std::this_thread::yield();
        }
        popped.push_back(*val);
    }
    
    stealer.join();
    
    EXPECT_EQ(stolen.size() + popped.size(), count);
    
    std::set<int> all_items;
    all_items.insert(stolen.begin(), stolen.end());
    all_items.insert(popped.begin(), popped.end());
    EXPECT_EQ(all_items.size(), count);
}
