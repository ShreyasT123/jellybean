#include <gtest/gtest.h>
#include "jellybean/reactor/reactor.hpp"
#include <chrono>
#include <thread>

using namespace jellybean::reactor;

TEST(TimerWheelTest, BasicTimer) {
    TimerWheel wheel;
    bool called = false;
    
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    
    wheel.initialize(now_ns);
    
    wheel.add_timer(10 * 1000000, [&]() { // 10ms
        called = true;
    });
    
    wheel.advance(now_ns + 5 * 1000000); // 5ms passed
    EXPECT_FALSE(called);
    
    wheel.advance(now_ns + 15 * 1000000); // 15ms passed
    EXPECT_TRUE(called);
}

TEST(ReactorTest, RunStop) {
    Reactor reactor(nullptr); // Use null backend
    
    int count = 0;
    reactor.add_timer(10 * 1000000, [&]() {
        count++;
        reactor.stop();
    });
    
    reactor.run();
    EXPECT_EQ(count, 1);
}
