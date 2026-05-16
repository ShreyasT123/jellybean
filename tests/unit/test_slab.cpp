#include <gtest/gtest.h>
#include "jellybean/memory/slab.hpp"

using namespace jellybean::memory;

struct MockObject {
    int id;
    bool* destroyed;
    MockObject(int id, bool* destroyed) : id(id), destroyed(destroyed) {
        *destroyed = false;
    }
    ~MockObject() {
        *destroyed = true;
    }
};

TEST(SlabAllocatorTest, BasicAllocation) {
    SlabAllocator<MockObject, 2> slab;
    bool d1, d2, d3;
    
    MockObject* o1 = slab.allocate(1, &d1);
    MockObject* o2 = slab.allocate(2, &d2);
    
    ASSERT_NE(o1, nullptr);
    ASSERT_NE(o2, nullptr);
    ASSERT_NE(o1, o2);
    EXPECT_EQ(o1->id, 1);
    EXPECT_EQ(o2->id, 2);
    
    // This should trigger a second slab
    MockObject* o3 = slab.allocate(3, &d3);
    ASSERT_NE(o3, nullptr);
    EXPECT_EQ(o3->id, 3);
    
    slab.deallocate(o1);
    EXPECT_TRUE(d1);
    
    // Allocate again, should reuse o1's slot
    MockObject* o4 = slab.allocate(4, &d1);
    EXPECT_EQ(o4, o1);
    EXPECT_FALSE(d1);
}

TEST(SlabAllocatorTest, MassiveAllocation) {
    SlabAllocator<size_t, 100> slab;
    std::vector<size_t*> ptrs;
    
    for (size_t i = 0; i < 1000; ++i) {
        ptrs.push_back(slab.allocate(i));
    }
    
    for (size_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(*ptrs[i], i);
    }
    
    for (auto p : ptrs) {
        slab.deallocate(p);
    }
}
