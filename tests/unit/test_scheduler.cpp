#include <gtest/gtest.h>
#include "jellybean/scheduler/scheduler.hpp"
#include <thread>

using namespace jellybean::scheduler;

TEST(SchedulerTest, CpuTopologyDetect) {
    CpuTopology topo = CpuTopology::detect();
    EXPECT_GT(topo.cores.size(), 0);
    EXPECT_EQ(topo.cores.size(), std::thread::hardware_concurrency());
}

TEST(SchedulerTest, ThreadPinning) {
    // Just ensure it doesn't crash.
    pin_thread_to_cpu(0);
}
