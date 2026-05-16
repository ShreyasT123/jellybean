#pragma once
#include <vector>
#include <cstdint>

namespace jellybean::scheduler {

/**
 * @brief Represents a logical CPU core and its properties.
 */
struct CpuCore {
    int cpu_id;        // Logical CPU index (0 to N-1)
    int physical_id;   // Physical core index
    int numa_node;     // NUMA node index
    bool is_p_core;    // True if it's a Performance core (hybrid arch)
};

/**
 * @brief Detects the CPU topology of the system.
 */
struct CpuTopology {
    std::vector<CpuCore> cores;

    static CpuTopology detect();
};

/**
 * @brief Pins the current thread to a specific CPU core.
 */
void pin_thread_to_cpu(int cpu_id);

} // namespace jellybean::scheduler
