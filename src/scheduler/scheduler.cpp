#include "jellybean/scheduler/scheduler.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fstream>
#include <string>

namespace jellybean::scheduler {

auto CpuTopology::detect() -> CpuTopology {
    CpuTopology topo;
    unsigned int n = std::thread::hardware_concurrency();

    // For now, a simple detection. Real detection would parse sysfs on Linux.
    for (unsigned int i = 0; i < n; ++i) {
        CpuCore core;
        core.cpu_id = i;
        core.physical_id = i;   // Simplified
        core.numa_node = 0;     // Simplified
        core.is_p_core = true;  // Assume all are P-cores for now
        topo.cores.push_back(core);
    }

    return topo;
}

void pin_thread_to_cpu(int cpu_id) {
    if (cpu_id < 0) {
        std::abort();
    }
    const int max_cpus = static_cast<int>(CPU_SETSIZE);
    if (cpu_id >= max_cpus) {
        std::abort();
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        std::abort();
    }
}

}  // namespace jellybean::scheduler
