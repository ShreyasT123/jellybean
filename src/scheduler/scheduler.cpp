#include "jellybean/scheduler/scheduler.hpp"
#include <thread>
#include <iostream>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fstream>
#include <string>
#endif

namespace jellybean::scheduler {

CpuTopology CpuTopology::detect() {
    CpuTopology topo;
    unsigned int n = std::thread::hardware_concurrency();
    
    // For now, a simple detection. Real detection would parse sysfs on Linux.
    for (unsigned int i = 0; i < n; ++i) {
        CpuCore core;
        core.cpu_id = i;
        core.physical_id = i; // Simplified
        core.numa_node = 0;   // Simplified
        core.is_p_core = true; // Assume all are P-cores for now
        topo.cores.push_back(core);
    }
    
    return topo;
}

void pin_thread_to_cpu(int cpu_id) {
    if (cpu_id < 0) {
        std::abort();
    }
#if defined(_WIN32)
    if (cpu_id >= static_cast<int>(sizeof(DWORD_PTR) * 8)) {
        std::abort();
    }
    HANDLE thread = GetCurrentThread();
    DWORD_PTR mask = 1ULL << cpu_id;
    if (SetThreadAffinityMask(thread, mask) == 0) {
        std::abort();
    }
#else
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
#endif
}

} // namespace jellybean::scheduler
