#include <gtest/gtest.h>
#include "jellybean/memory/arena.hpp"
#include <vector>

using namespace jellybean::memory;

TEST(ArenaAllocatorTest, BasicAllocation) {
    ArenaAllocator arena(1024);
    
    void* p1 = arena.allocate(128);
    ASSERT_NE(p1, nullptr);
    
    void* p2 = arena.allocate(128);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p1, p2);
    
    // Check alignment
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p1) % alignof(std::max_align_t), 0);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p2) % alignof(std::max_align_t), 0);
}

TEST(ArenaAllocatorTest, ConstructObjects) {
    ArenaAllocator arena(1024);
    
    struct Dummy {
        int a;
        double b;
        Dummy(int a, double b) : a(a), b(b) {}
    };
    
    Dummy* d = arena.construct<Dummy>(42, 3.14);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->a, 42);
    EXPECT_DOUBLE_EQ(d->b, 3.14);
}

TEST(ArenaAllocatorTest, Reset) {
    ArenaAllocator arena(1024);
    
    void* p1 = arena.allocate(512);
    arena.reset();
    void* p2 = arena.allocate(512);
    
    // After reset, p2 might be the same as p1 if it was the start of the block
    EXPECT_EQ(p1, p2);
}

TEST(ArenaAllocatorTest, MultiBlockAllocation) {
    ArenaAllocator arena(1024);
    
    // Allocate more than 1024 bytes to trigger slow path
    void* p1 = arena.allocate(800);
    void* p2 = arena.allocate(800);
    
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p1, p2);
    
    // bytes_allocated() returns total used across blocks
    EXPECT_GE(arena.bytes_allocated(), 1600);
    // total_allocated_ is private, but we check wasted
    EXPECT_GE(arena.bytes_wasted(), 400); // roughly (1024-800) + (1024-800)
}

TEST(ArenaAllocatorTest, Alignment) {
    ArenaAllocator arena(1024);
    
    [[maybe_unused]] void* p1 = arena.allocate(1, 1);
    void* p2 = arena.allocate(1, 64);
    
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0);
}
