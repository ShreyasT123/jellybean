#include <gtest/gtest.h>
#include "jellybean/concurrency/seqlock.hpp"
#include <thread>
#include <vector>

using namespace jellybean::concurrency;

struct Config {
    int a;
    int b;
    int c;
};

TEST(SeqlockTest, BasicReadWrite) {
    Seqlock lock;
    Config config{1, 2, 3};
    
    lock.write([&]() {
        config.a = 10;
        config.b = 20;
        config.c = 30;
    });
    
    Config read_config = lock.read<Config>([&]() {
        return config;
    });
    
    EXPECT_EQ(read_config.a, 10);
    EXPECT_EQ(read_config.b, 20);
    EXPECT_EQ(read_config.c, 30);
}

TEST(SeqlockTest, ConcurrentReadWrite) {
    Seqlock lock;
    Config config{0, 0, 0};
    const int iterations = 100000;
    
    std::thread writer([&]() {
        for (int i = 0; i < iterations; ++i) {
            lock.write([&]() {
                config.a = i;
                config.b = i;
                config.c = i;
            });
        }
    });
    
    std::thread reader([&]() {
        for (int i = 0; i < iterations; ++i) {
            Config c = lock.read<Config>([&]() {
                return config;
            });
            // Ensure we never see inconsistent state
            EXPECT_EQ(c.a, c.b);
            EXPECT_EQ(c.a, c.c);
        }
    });
    
    writer.join();
    reader.join();
}
