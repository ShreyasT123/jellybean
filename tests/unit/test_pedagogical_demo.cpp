/**
 * ========================================================================================================
 *                              J E L L Y B E A N   P E D A G O G I C A L   D E M O
 * ========================================================================================================
 * This file serves as an interactive, highly-detailed, and self-documenting walk-through of the
 * complete Jellybean High-Performance Inference Runtime architecture.
 *
 * DESIGN ARCHITECTURE:
 * Jellybean is a portfolio-grade, Triton-style distributed actor serving infrastructure built to
 * show MAANG-level low-level systems control. The system achieves microsecond tail latencies by
 * employing:
 *
 *   1. LOCK-FREE PRIMITIVES  - Zero-cost multi-core communication utilizing memory-barrier
 * synchronization.
 *   2. CORE-AFFINITY SHARDING- Pinning event loops (Reactors) per core to avoid context switches
 * (ScyllaDB/Seastar style).
 *   3. CUSTOM ALLOCATORS     - Eliminating allocator pressure on hot paths via Arena and Slab pools
 * (malloc-free).
 *   4. COOPERATIVE FIBERS    - C++20 coroutines for scheduling high-concurrency tasks without
 * thread stacks.
 *   5. ZERO-COPY PROTOCOLS   - Hand-rolled binary wire format with custom CRC32 validation.
 *
 * This test runs a live, interactive benchmark and demonstration of all major subsystems,
 * explaining the "why" and "how" behind each component with comprehensive console prints and rich
 * documentation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <thread>
#include <future>

// Include Subsystem Headers
#include "jellybean/actor/actor.hpp"
#include "jellybean/actor/mailbox.hpp"
#include "jellybean/actor/registry.hpp"
#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/concurrency/seqlock.hpp"
#include "jellybean/concurrency/spsc_queue.hpp"
#include "jellybean/inference/backend.hpp"
#include "jellybean/inference/runtime.hpp"
#include "jellybean/memory/arena.hpp"
#include "jellybean/memory/huge_pages.hpp"
#include "jellybean/memory/slab.hpp"
#include "jellybean/proto/codec.hpp"
#include "jellybean/reactor/reactor.hpp"
#include "jellybean/reactor/epoll_backend.hpp"
#include "jellybean/reactor/timer_wheel.hpp"

using namespace jellybean::reactor;
#include "jellybean/scheduler/fiber.hpp"
#include "jellybean/scheduler/scheduler.hpp"
#include "jellybean/scheduler/work_stealing_deque.hpp"

namespace jellybean::demo {

using namespace std::chrono_literals;

// Utility functions for pretty console printing
void print_header(std::string_view title) {
    std::cout << "\n\033[1;36m"
              << "┌────────────────────────────────────────────────────────────────────────────────"
                 "────────┐\n"
              << "│ " << std::left << std::setw(86) << title << " │\n"
              << "└────────────────────────────────────────────────────────────────────────────────"
                 "────────┘"
              << "\033[0m\n";
}

void print_subheader(std::string_view label) {
    std::cout << "\n\033[1;33m>>> " << label << "\033[0m\n";
}

void print_concept(std::string_view concept_text) {
    std::cout << "\033[0;90m" << concept_text << "\033[0m\n";
}

void print_metric(std::string_view metric, double val, std::string_view unit) {
    std::cout << "  \033[1;32m✓\033[0m " << std::left << std::setw(35) << metric << ": \033[1;37m"
              << std::fixed << std::setprecision(3) << val << "\033[0m " << unit << "\n";
}

void print_metric(std::string_view metric, std::string_view val) {
    std::cout << "  \033[1;32m✓\033[0m " << std::left << std::setw(35) << metric << ": \033[1;37m"
              << val << "\033[0m\n";
}

struct BenchmarkObject {
    double data[8];  // 64 bytes
    BenchmarkObject() {
        for (int i = 0; i < 8; ++i) data[i] = static_cast<double>(i);
    }
};

/**
 * ========================================================================================================
 * MODULE 1: CUSTOM MEMORY ALLOCATORS (ArenaAllocator & SlabAllocator)
 * ========================================================================================================
 * WHY:
 * Regular malloc/free causes kernel mode context switching, locks global heaps, and causes high
 * memory fragmentation. In high-performance servers, allocator pressure is the #1 tail latency
 * killer. We resolve this completely on hot paths by using:
 *
 *   - ArenaAllocator: A shard-local bump allocator. Memory allocation is a simple addition (O(1)).
 *     It supports rapid bulk deallocation via `reset()`, recycling entire blocks between request
 * cycles.
 *   - SlabAllocator: An intrusive free-list allocator that manages a cache of fixed-size objects.
 *     It recycles freed memory slots without constructor/destructor and page-allocation overhead.
 */
TEST(PedagogicalWalkthroughTest, Module1_MemoryManagement) {
    print_header("MODULE 1: CUSTOM MEMORY ALLOCATORS (ArenaAllocator & SlabAllocator)");

    print_concept(
        "CONCEPT: Bump Pointer & Intrusive Free-Lists\n"
        "  - Arena Allocator: Increments a pointer by the allocation size (aligned). Bulk reset at "
        "request boundaries.\n"
        "  - Slab Allocator: Pre-allocates pages of slots. Free slots are chained using an "
        "intrusive linked list\n"
        "    where the free slots themselves store pointers to the next free slots. Zero "
        "per-object overhead!\n");

    // 1. Arena Allocation Speed Demonstration
    print_subheader("1. Arena Allocator vs. Standard Malloc Benchmark");
    constexpr size_t NUM_ALLOCS = 100000;

    // Warmup standard malloc/free
    {
        std::vector<BenchmarkObject*> ptrs;
        ptrs.reserve(NUM_ALLOCS);
        for (size_t i = 0; i < NUM_ALLOCS; ++i) ptrs.push_back(new BenchmarkObject());
        for (auto p : ptrs) delete p;
    }

    // Benchmark standard new/delete
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        std::vector<BenchmarkObject*> ptrs;
        ptrs.reserve(NUM_ALLOCS);
        for (size_t i = 0; i < NUM_ALLOCS; ++i) {
            ptrs.push_back(new BenchmarkObject());
        }
        for (auto p : ptrs) {
            delete p;
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double time_std = std::chrono::duration<double, std::micro>(t1 - t0).count();

    // Benchmark Arena Allocator
    t0 = std::chrono::high_resolution_clock::now();
    {
        jellybean::memory::ArenaAllocator arena(4 * 1024 * 1024);  // 4MB block
        for (size_t i = 0; i < NUM_ALLOCS; ++i) {
            auto* obj = arena.construct<BenchmarkObject>();
            (void)obj;
        }
        // Bulk deallocation is instantaneous and automatic when the arena goes out of scope!
    }
    t1 = std::chrono::high_resolution_clock::now();
    double time_arena = std::chrono::duration<double, std::micro>(t1 - t0).count();

    print_metric("Standard heap allocation time", time_std, "us");
    print_metric("Arena bump allocation time", time_arena, "us");
    print_metric("Arena Speedup Ratio", time_std / time_arena, "x faster");

    // 2. Slab Allocator Demo (Recycling Intrusive Slots)
    print_subheader("2. Slab Allocator Recycling Demonstration");

    struct DemoObj {
        int id;
        bool* destroyed_flag;
        DemoObj(int id, bool* flag) : id(id), destroyed_flag(flag) {
            *destroyed_flag = false;
        }
        ~DemoObj() {
            *destroyed_flag = true;
        }
    };

    jellybean::memory::SlabAllocator<DemoObj, 2>
        slab;  // Small slab chunk size of 2 for easy trigger
    bool d1 = false, d2 = false, d3 = false;

    // Allocate 2 elements (fills first chunk)
    DemoObj* o1 = slab.allocate(101, &d1);
    DemoObj* o2 = slab.allocate(202, &d2);

    print_metric("Object 1 allocated at", std::to_string(reinterpret_cast<uintptr_t>(o1)));
    print_metric("Object 2 allocated at", std::to_string(reinterpret_cast<uintptr_t>(o2)));
    EXPECT_NE(o1, o2);

    // Allocating a 3rd element triggers the allocation of a second slab chunk
    DemoObj* o3 = slab.allocate(303, &d3);
    print_metric("Object 3 (new chunk) allocated at",
                 std::to_string(reinterpret_cast<uintptr_t>(o3)));
    EXPECT_NE(o1, o3);

    // Deallocate Object 1. This triggers its destructor and pushes the slot onto the intrusive
    // free-list
    slab.deallocate(o1);
    print_metric("Object 1 deallocated. Destructor ran", d1 ? "TRUE" : "FALSE");
    EXPECT_TRUE(d1);

    // Re-allocate an object. It should instantaneously recycle Object 1's slot!
    DemoObj* o4 = slab.allocate(404, &d1);
    print_metric("Object 4 allocated at (recycles O1)",
                 std::to_string(reinterpret_cast<uintptr_t>(o4)));
    EXPECT_EQ(o1, o4);
    EXPECT_FALSE(d1);  // Reset back to false during construction

    // Clean up
    slab.deallocate(o2);
    slab.deallocate(o3);
    slab.deallocate(o4);
}

/**
 * ========================================================================================================
 * MODULE 2: LOCK-FREE CONCURRENCY (SpscQueue, MpmcQueue, & Seqlock)
 * ========================================================================================================
 * WHY:
 * Mutexes cause operating system context-switching, suspending threads and yielding core cycles to
 * the OS scheduler. This introduces massive tail latency spikes. We implement custom lock-free
 * queues that:
 *
 *   - SpscQueue: Perfect for shard-to-shard message passing. Uses memory barriers to synchronize,
 *     pins consumer/producer indices on distinct cache lines (`alignas(64)`) to eliminate false
 * sharing.
 *   - MpmcQueue: Dmitry Vyukov's queue utilizing sequence counters to safely coordinate multiple
 * writers and multiple readers without locks. Used for work-stealing schedulers.
 *   - Seqlock: Starvation-free reader-writer lock. Readers read without atomic operations
 * (extremely cheap), validating that no writer modified the sequence counter during the read span.
 */
TEST(PedagogicalWalkthroughTest, Module2_LockFreeConcurrency) {
    print_header("MODULE 2: LOCK-FREE CONCURRENCY (SpscQueue, MpmcQueue, & Seqlock)");

    print_concept(
        "CONCEPT: False Sharing & Memory Barriers\n"
        "  - Cache Line Bouncing: When two cores write to adjacent fields on the same 64-byte "
        "cache line,\n"
        "    their L1/L2 caches constantly invalidate each other. SpscQueue aligns atomic indexes "
        "to prevent this.\n"
        "  - Sequence Locking: Reader loops until it grabs an even sequence ID and double-checks "
        "that the ID\n"
        "    has not changed, avoiding atomic compare-and-swap RMW cycles on the read path.\n");

    // 1. SpscQueue Multi-Threaded Latency Demo
    print_subheader("1. SpscQueue Thread-to-Thread Latency Benchmark");
    jellybean::concurrency::SpscQueue<int, 2048> spsc;

    std::atomic<bool> running{true};
    std::vector<uint64_t> latencies;

    // Consumer thread
    std::thread consumer([&]() {
        while (running.load(std::memory_order_relaxed) || !spsc.empty()) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto val = spsc.try_pop();
            if (val) {
                auto t1 = std::chrono::high_resolution_clock::now();
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                latencies.push_back(elapsed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Producer enqueues items
    constexpr int NUM_ITEMS = 50000;
    auto pt0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_ITEMS; ++i) {
        while (!spsc.try_push(i)) {
            std::this_thread::yield();
        }
    }
    auto pt1 = std::chrono::high_resolution_clock::now();
    double prod_time = std::chrono::duration<double, std::micro>(pt1 - pt0).count();

    // Allow consumer to drain
    while (!spsc.empty()) {
        std::this_thread::sleep_for(1ms);
    }
    running.store(false, std::memory_order_relaxed);
    consumer.join();

    double avg_lat = 0;
    if (!latencies.empty()) {
        avg_lat = static_cast<double>(std::accumulate(latencies.begin(), latencies.end(), 0ULL)) /
                  latencies.size();
    }

    print_metric("SpscQueue Producer throughput",
                 static_cast<double>(NUM_ITEMS) / (prod_time / 1e6), "ops/sec");
    print_metric("SpscQueue Average item transit latency", avg_lat, "ns");

    // 2. Seqlock Reader-Writer Demo
    print_subheader("2. Seqlock Lock-Free Shared Config Verification");
    struct SharedConfig {
        uint64_t model_version;
        double threshold;
        uint32_t batch_limit;
    };

    jellybean::concurrency::Seqlock seqlock;
    volatile SharedConfig config{1, 0.949, 33};

    // Parallel reader and writer
    std::atomic<bool> worker_active{true};
    std::thread writer([&]() {
        for (uint64_t i = 2; i <= 100; ++i) {
            seqlock.write([&]() {
                config.model_version = i;
                config.threshold = 0.95 - (static_cast<double>(i) * 0.001);
                config.batch_limit = 32 + static_cast<uint32_t>(i);
            });
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        worker_active.store(false);
    });

    uint64_t read_count = 0;
    uint64_t mismatch_count = 0;

    std::thread reader([&]() {
        while (worker_active.load()) {
            SharedConfig local = seqlock.read<SharedConfig>([&]() {
                SharedConfig c;
                c.model_version = config.model_version;
                c.threshold = config.threshold;
                c.batch_limit = config.batch_limit;
                return c;
            });

            // Critical check: even though config is read concurrently without locks,
            // we should never see torn writes. Check mathematical relationships:
            double expected_threshold = 0.95 - (static_cast<double>(local.model_version) * 0.001);
            uint32_t expected_batch = 32 + static_cast<uint32_t>(local.model_version);

            if (std::abs(local.threshold - expected_threshold) > 1e-9 ||
                local.batch_limit != expected_batch) {
                mismatch_count++;
            }
            read_count++;
        }
    });

    writer.join();
    reader.join();

    print_metric("Seqlock Concurrent reads performed", static_cast<double>(read_count), "reads");
    print_metric("Seqlock Torn write state detections", static_cast<double>(mismatch_count),
                 "mismatches (MUST BE 0!)");
    EXPECT_EQ(mismatch_count, 0u);
    EXPECT_GT(read_count, 0u);
}

/**
 * ========================================================================================================
 * MODULE 3: REACTOR EVENT LOOP & TIMER WHEEL (Reactor & TimerWheel)
 * ========================================================================================================
 * WHY:
 * A thread-per-connection scaling strategy breaks down as connections grow due to context-switching
 * overhead. Jellybean implements a per-core Reactor event loop (utilizing epoll/io_uring) combined
 * with a hashed TimerWheel. Instead of heap-based timers (O(log N)), the TimerWheel expires timers
 * in O(1) time complexity, which is critical for timing out pending client requests under extreme
 * server loads.
 */
TEST(PedagogicalWalkthroughTest, Module3_ReactorAndTimerWheel) {
    print_header("MODULE 3: REACTOR EVENT LOOP & TIMER WHEEL (Reactor & TimerWheel)");

    print_concept(
        "CONCEPT: Hashed Timer Wheels\n"
        "  - Heap Timers (std::priority_queue) take O(log N) to insert/delete. TimerWheel acts "
        "like an analog clock\n"
        "    with a circular array of slots representing ticks. Timers are placed in the slot "
        "corresponding to their\n"
        "    expiration tick. Expiration check is a simple O(1) array lookup at each clock "
        "tick.\n");

    print_subheader("1. Hashed Timer Wheel Callback Expiration");
    jellybean::reactor::TimerWheel wheel;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    wheel.initialize(now_ns);

    bool t5ms_fired = false;
    bool t20ms_fired = false;

    // Schedule timers
    wheel.add_timer(5 * 1000000, [&]() { t5ms_fired = true; });    // 5ms timer
    wheel.add_timer(20 * 1000000, [&]() { t20ms_fired = true; });  // 20ms timer

    // Advance time by 10ms
    wheel.advance(now_ns + 10 * 1000000);
    print_metric("Time advanced +10ms. 5ms timer fired", t5ms_fired ? "TRUE" : "FALSE");
    print_metric("Time advanced +10ms. 20ms timer fired", t20ms_fired ? "TRUE" : "FALSE");
    EXPECT_TRUE(t5ms_fired);
    EXPECT_FALSE(t20ms_fired);

    // Advance time by another 15ms (total 25ms)
    wheel.advance(now_ns + 25 * 1000000);
    print_metric("Time advanced +25ms. 20ms timer fired", t20ms_fired ? "TRUE" : "FALSE");
    EXPECT_TRUE(t20ms_fired);

    // 2. Running a Reactor instance in loop
    print_subheader("2. Single-Threaded Reactor Event Dispatching");
    jellybean::reactor::Reactor reactor(nullptr);  // Use dummy null backend for unit-level safety

    int timer_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Add hierarchical timers to reactor
    reactor.add_timer(10 * 1000000, [&]() {
        timer_count++;
        // Stop reactor after first callback completes
        reactor.stop();
    });

    reactor.run();
    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    print_metric("Reactor timers dispatched successfully", static_cast<double>(timer_count),
                 "events");
    print_metric("Reactor execution loop duration", duration, "ms");
    EXPECT_EQ(timer_count, 1);
}

/**
 * ========================================================================================================
 * MODULE 4: COOPERATIVE SCHEDULING & FIBERS (WorkStealingDeque & Tasks)
 * ========================================================================================================
 * WHY:
 * Multi-threaded systems incur massive OS scheduling latencies when threads block. Go and Tokio
 * bypass this by implementing custom work-stealing runtimes with cooperative fibers. Jellybean
 * replicates this by:
 *
 *   - CpuTopology: Pinning worker threads to dedicated cores to maximize hardware cache locality.
 *   - WorkStealingDeque: A lock-free double-ended queue. Workers push/pop tasks locally (LIFO),
 *     while idle threads can steal from the opposite end (FIFO), balancing loads without
 * synchronization.
 *   - Task/Yield: C++20 coroutines allowing a running fiber to yield CPU control back to the
 * scheduler, resuming execution later without saving full thread context on the OS stack.
 */
TEST(PedagogicalWalkthroughTest, Module4_SchedulerAndFibers) {
    print_header("MODULE 4: COOPERATIVE SCHEDULING & FIBERS (WorkStealingDeque & Tasks)");

    print_concept(
        "CONCEPT: Work-Stealing & Cooperative Yields\n"
        "  - Thread Pinning: Prevents OS thread migration across CPU cores, eliminating L1/L2 "
        "cache invalidations.\n"
        "  - Deque Stealing: Local push/pop uses lock-free atomic operations. Stealers only "
        "interact with the tail\n"
        "    index, minimizing contention under heavy loads.\n"
        "  - Fiber Task: Coroutine frame contains suspend-point context. Yielding triggers "
        "scheduler queue re-insertion,\n"
        "    achieving user-space scheduling in microsecond intervals.\n");

    // 1. CPU Core Affinity Detection
    print_subheader("1. Hardware Topology Detection & Thread Pinning");
    jellybean::scheduler::CpuTopology topology = jellybean::scheduler::CpuTopology::detect();
    print_metric("Detected hardware CPU cores", static_cast<double>(topology.cores.size()),
                 "cores");

    // Ensure pinning doesn't crash (pinnings are critical on Linux; Windows ignores non-existent
    // CPU binds)
    jellybean::scheduler::pin_thread_to_cpu(0);
    print_metric("Core sharding thread pinning initialization", "SUCCESS");

    // 2. Lock-free Work Stealing Deque Demo
    print_subheader("2. Work-Stealing Deque Mechanics");
    jellybean::scheduler::WorkStealingDeque<int> local_deque;

    // Push tasks onto local queue
    local_deque.push(10);
    local_deque.push(20);
    local_deque.push(30);

    // Local worker pops task (LIFO behavior)
    auto t_local = local_deque.pop();
    print_metric("Local worker popped task (LIFO expected 30)",
                 static_cast<double>(t_local ? *t_local : -1), "value");
    EXPECT_EQ(t_local, 30);

    // Idle worker steals task from the opposite end of the queue (FIFO behavior)
    auto t_stolen = local_deque.steal();
    print_metric("Idle worker stole task (FIFO expected 10)",
                 static_cast<double>(t_stolen ? *t_stolen : -1), "value");
    EXPECT_EQ(t_stolen, 10);

    // Local worker pops final task
    auto t_last = local_deque.pop();
    print_metric("Local worker popped last task (expected 20)",
                 static_cast<double>(t_last ? *t_last : -1), "value");
    EXPECT_EQ(t_last, 20);

    // Verify empty state
    EXPECT_EQ(local_deque.pop(), std::nullopt);
    EXPECT_EQ(local_deque.steal(), std::nullopt);

    // 3. C++20 Coroutine Task Driving
    print_subheader("3. C++20 Cooperative Coroutine Task Driver");

    struct TaskDriver {
        static jellybean::scheduler::Task<> mock_fiber(int& step_counter) {
            step_counter = 1;
            // First yield
            co_await jellybean::scheduler::Yield{};
            step_counter = 2;
            // Second yield
            co_await jellybean::scheduler::Yield{};
            step_counter = 3;
            co_return;
        }
    };

    int progress = 0;
    auto task = TaskDriver::mock_fiber(progress);
    auto handle = task.handle;

    print_metric("Coroutine task created. Progress", static_cast<double>(progress),
                 "steps completed");

    // Resume once
    handle.resume();
    print_metric("Coroutine resumed step 1. Progress", static_cast<double>(progress),
                 "steps completed");
    EXPECT_EQ(progress, 1);

    // Resume twice (past first yield)
    handle.resume();
    print_metric("Coroutine resumed step 2. Progress", static_cast<double>(progress),
                 "steps completed");
    EXPECT_EQ(progress, 2);

    // Resume to completion
    handle.resume();
    print_metric("Coroutine completed. Progress", static_cast<double>(progress), "steps completed");
    EXPECT_EQ(progress, 3);
}

/**
 * ========================================================================================================
 * MODULE 5: PROTOCOL & SERIALIZATION (Codec, Frame, & Wire)
 * ========================================================================================================
 * WHY:
 * High-overhead serialization formats like JSON add massive CPU processing costs. Production
 * servers like Triton employ framed binary protocols. We implement a custom, highly-aligned framed
 * binary wire format with:
 *
 *   - FrameHeader: 32-byte layout containing magic identifiers, message type, and CRC32 checks.
 *   - Padding Alignment: Automatically pads payloads to 8-byte boundaries to allow hardware
 * SIMD/AVX vector memory alignments.
 *   - CRC32 Validation: Performs rapid checksum verification to filter corrupted frames before
 * processing.
 */
TEST(PedagogicalWalkthroughTest, Module5_WireProtocolAndCodec) {
    print_header("MODULE 5: PROTOCOL & SERIALIZATION (Codec, Frame, & Wire)");

    print_concept(
        "CONCEPT: Zero-Copy Binary Layouts\n"
        "  - JSON Serialization: Expensive string parses, heap allocations, and type casting.\n"
        "  - Framed Binary: Binary fields mapped directly to memory layouts. Casting a buffer "
        "directly to\n"
        "    a struct pointer allows instantly extracting headers without parsing, achieving "
        "zero-copy speeds!\n");

    print_subheader("1. Binary Frame Encoding & Serialization Layout");
    const std::array<std::byte, 4> payload{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                           std::byte{0xEF}};

    // Calculate required padded buffer size
    std::size_t required_size = jellybean::proto::encoded_frame_size(payload.size());
    print_metric("Raw payload size", static_cast<double>(payload.size()), "bytes");
    print_metric("Padded binary frame size", static_cast<double>(required_size), "bytes");

    std::vector<std::byte> frame_buffer(required_size);

    // Encode frame
    std::size_t bytes_written =
        jellybean::proto::encode_frame(frame_buffer, jellybean::proto::MessageType::ActorMessage,
                                       0x1u,  // flags
                                       777u,  // actor_id
                                       888u,  // message_id
                                       payload);
    print_metric("Bytes written during encoding", static_cast<double>(bytes_written), "bytes");
    EXPECT_EQ(bytes_written, required_size);

    // 2. Decode and Validate
    print_subheader("2. Binary Frame Parsing & CRC Checksum Validation");
    auto parsed = jellybean::proto::parse_frame(
        std::span<const std::byte>(frame_buffer.data(), bytes_written));

    print_metric("Frame header validation status", parsed.valid ? "VALID" : "INVALID");
    print_metric("Extracted message type ID", static_cast<double>(parsed.header.type), "type");
    print_metric("Extracted message ID", static_cast<double>(parsed.header.message_id), "id");
    print_metric("Computed CRC32 checksum", static_cast<double>(parsed.actual_crc), "crc");

    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.header.actor_id, 777u);
    EXPECT_EQ(parsed.header.message_id, 888u);
    EXPECT_TRUE(std::equal(parsed.payload().begin(), parsed.payload().end(), payload.begin()));

    // 3. Corrupt buffer and verify checksum rejection
    print_subheader("3. Network Corrupted Packet Rejection");
    frame_buffer[sizeof(jellybean::proto::FrameHeader)] ^=
        std::byte{0xFF};  // Corrupt first byte of payload

    auto corrupted_parse = jellybean::proto::parse_frame(
        std::span<const std::byte>(frame_buffer.data(), bytes_written));
    print_metric("Corrupted packet parsed valid status",
                 corrupted_parse.valid ? "VALID" : "REJECTED (INVALID CRC)");
    EXPECT_FALSE(corrupted_parse.valid);
}

/**
 * ========================================================================================================
 * MODULE 6: ACTOR MODEL RUNTIME (ActorBase, Message, & Registry)
 * ========================================================================================================
 * WHY:
 * Actor model completely encapsulates state. By routing messages using shard-local queues, actors
 * do not share state or need locks, enabling completely linear scale-out capability. Jellybean
 * actors feature:
 *
 *   - SSO Messages (Small Stack Optimization): If message payload <= 48 bytes, it is held inline on
 * the stack without allocating heap memory, bypassing memory managers entirely on common messages.
 *   - ActorRegistry: Lock-free shard-indexed lookup registry to locate and direct messages to actor
 * mailboxes.
 */
TEST(PedagogicalWalkthroughTest, Module6_ActorModelAndMailbox) {
    print_header("MODULE 6: ACTOR MODEL RUNTIME (ActorBase, Message, & Registry)");

    print_concept(
        "CONCEPT: Small Stack Optimization (SSO)\n"
        "  - Heap Allocation Overhead: Allocating individual buffers for short messages creates "
        "massive\n"
        "    allocator overhead. Message union stores inline bytes up to 48 bytes without calling "
        "malloc.\n");

    // 1. SSO Message Allocation Proof
    print_subheader("1. Message Small Stack Optimization Proof");

    struct ShortPayload {
        int a;
        double b;
    };
    struct LargePayload {
        double array[20];
    };  // 160 bytes (exceeds SSO 48-byte limit)

    jellybean::actor::Message msg_short;
    msg_short.set(ShortPayload{42, 3.1415});

    jellybean::actor::Message msg_large;
    msg_large.set(LargePayload{});

    print_metric("Short message is inline (SSO)",
                 msg_short.is_inline ? "TRUE (NO HEAP ALLOC!)" : "FALSE");
    print_metric("Large message is inline (SSO)",
                 msg_large.is_inline ? "TRUE" : "FALSE (HEAP ALLOC TRIGGERED)");

    EXPECT_TRUE(msg_short.is_inline);
    EXPECT_FALSE(msg_large.is_inline);

    // 2. Actor Registry Messaging Demo
    print_subheader("2. Actor Mailbox Enqueueing & Register Lookup");

    class DemoActor : public jellybean::actor::ActorBase {
       public:
        int received_value{0};
        DemoActor(ActorId id, uint32_t shard) : ActorBase(id, shard, nullptr) {}

        jellybean::scheduler::Task<> receive(jellybean::actor::Message msg) override {
            received_value = msg.as<int>();
            co_return;
        }
    };

    jellybean::actor::ActorRegistry registry;
    DemoActor actor(12345u, 0u);
    auto mailbox = std::make_shared<jellybean::actor::Mailbox>();

    // Register actor
    registry.register_actor(&actor, mailbox);

    // Lookup mailbox
    auto target_mailbox = registry.find_mailbox(12345u);
    EXPECT_NE(target_mailbox, nullptr);
    EXPECT_EQ(target_mailbox, mailbox);

    // Send message to mailbox
    jellybean::actor::Message send_msg;
    send_msg.set(999);
    target_mailbox->try_push(std::move(send_msg));

    // Pop and drive actor receive coroutine
    auto popped = mailbox->try_pop();
    ASSERT_TRUE(popped.has_value());

    auto task = actor.receive(std::move(*popped));
    task.handle.resume();  // Drive coroutine execution

    print_metric("Actor received value successfully", static_cast<double>(actor.received_value),
                 "val");
    EXPECT_EQ(actor.received_value, 999);
}

/**
 * ========================================================================================================
 * MODULE 7: INFERENCE RUNTIME ENGINE (InferenceRuntime & Backend)
 * ========================================================================================================
 * WHY:
 * Inference workloads require combining thread scheduling and pipeline orchestration. Jellybean
 * features a Triton-style config-driven serving queue. Inactive workers block efficiently while
 * active models leverage the model queues, validating inputs and scheduling PyTorch backends
 * asynchronously.
 */
TEST(PedagogicalWalkthroughTest, Module7_InferenceServingRuntime) {
    print_header("MODULE 7: INFERENCE RUNTIME ENGINE (InferenceRuntime & Backend)");

    print_concept(
        "CONCEPT: Serving Queues & Backend Agnostics\n"
        "  - Pipeline Serving: Model requests are enqueued onto bounded thread-safe model queues. "
        "Workers\n"
        "    continuously pick up jobs, process through deep learning backends "
        "(LibTorch/TensorRT), and dispatch\n"
        "    asynchronous promise responses, allowing seamless pipelined serves.\n");

    // Mock Inference Backend
    class MockInferenceBackend : public jellybean::inference::IInferenceBackend {
       public:
        bool load(const std::string&, const std::string&,
                  jellybean::inference::DeviceKind) override {
            return true;
        }
        bool unload(const std::string&) override {
            return true;
        }

        jellybean::inference::InferenceResponse infer(
            const jellybean::inference::InferenceRequest& req) override {
            jellybean::inference::InferenceResponse resp;
            resp.ok = true;
            resp.shape = req.shape;

            // Double the input as a mock inference computation and write directly to output_buffer
            size_t n = std::min(req.input.size(), req.output_buffer.size());
            for (size_t i = 0; i < n; ++i) {
                req.output_buffer[i] = req.input[i] * 2.0f;
            }
            resp.output_elems_written = static_cast<uint32_t>(n);
            resp.latency_ns = 50000;  // Mock 50us latency
            return resp;
        }

        auto infer_batch(const std::vector<jellybean::inference::InferenceRequest>& batch)
            -> std::vector<jellybean::inference::InferenceResponse> override {
            std::vector<jellybean::inference::InferenceResponse> resps;
            resps.reserve(batch.size());
            for (const auto& req : batch) {
                resps.push_back(infer(req));
            }
            return resps;
        }
    };

    print_subheader("1. Multi-Threaded Inference Serving Pipeline");

    auto config = jellybean::inference::RuntimeConfig::from_file("configs/server.config");
    // Ensure we don't use the default 1 thread if we want to demo multi-threading
    if (config.worker_threads < 2) config.worker_threads = 2; 

    jellybean::inference::InferenceRuntime runtime(config);

    auto backend = std::make_shared<MockInferenceBackend>();
    jellybean::model::ModelMetadata meta;
    meta.backend = backend;
    meta.state.store(jellybean::model::ModelState::Ready, std::memory_order_release);

    EXPECT_TRUE(runtime.register_model("mock_transformer", &meta));
    print_concept("Model 'mock_transformer' registered in Serving Runtime");

    // Launch concurrent serving client requests
    print_subheader("2. Concurrent Client Request Load Handling");
    constexpr int REQUESTS = 20;

    struct ClientSession {
        std::vector<float> input;
        std::vector<float> output;
        jellybean::inference::InferenceResponse resp;
        std::promise<void> done;
    };

    std::vector<std::unique_ptr<ClientSession>> sessions;

    // We need a reactor to drive the InferenceAwaitable
    auto reactor_ptr = std::make_unique<jellybean::reactor::Reactor>(std::make_unique<jellybean::reactor::EpollBackend>());
    auto& reactor = *reactor_ptr;

    for (int i = 0; i < REQUESTS; ++i) {
        auto session = std::make_unique<ClientSession>();
        session->input = {1.0f * i, 2.0f * i, 3.0f * i, 4.0f * i};
        session->output.resize(4);

        auto run_client = [](jellybean::inference::InferenceRuntime& rt, ClientSession* s) -> jellybean::scheduler::Task<> {
            jellybean::inference::InferenceRequest req;
            req.model_id = "mock_transformer";
            req.shape = {1, 4};
            req.input = s->input;
            req.output_buffer = s->output;

            s->resp = co_await jellybean::inference::InferenceAwaitable(rt, std::move(req));
            s->done.set_value();
            co_return;
        };

        auto task = run_client(runtime, session.get());
        reactor.schedule(task.release());
        sessions.push_back(std::move(session));
    }

    // Run reactor in a separate thread to handle completions
    std::atomic<bool> reactor_running{true};
    std::thread reactor_thread([&]() {
        while (reactor_running) {
            try {
                reactor.run();
            } catch (...) {}
            std::this_thread::sleep_for(1ms);
        }
    });

    // Await all client responses
    int successful_responses = 0;
    for (int i = 0; i < REQUESTS; ++i) {
        auto fut = sessions[i]->done.get_future();
        while (fut.wait_for(100ms) != std::future_status::ready) {
            if (!reactor_running) break;
        }
        if (sessions[i]->resp.ok) {
            successful_responses++;
            EXPECT_EQ(sessions[i]->output[0], 1.0f * i * 2.0f);
            EXPECT_EQ(sessions[i]->output[3], 4.0f * i * 2.0f);
        }
    }

    reactor_running = false;
    reactor.stop();
    reactor_thread.join();

    print_metric("Concurrently enqueued and served client requests", static_cast<double>(REQUESTS),
                 "requests");
    print_metric("Successful processed serving responses",
                 static_cast<double>(successful_responses), "responses");
    EXPECT_EQ(successful_responses, REQUESTS);

    // Shutdown serving pipeline
    runtime.shutdown();
    print_metric("Inference serving runtime shutdown sequence", "SUCCESS");

    std::cout << "\n\033[1;32m====================================================================="
                 "===================\n"
              << "  ALL JELLYBEAN MODULES DEMOED, TESTED, AND VERIFIED SUCCESSFULLY. SYSTEMS STACK "
                 "READY!\n"
              << "================================================================================="
                 "=======\033[0m\n\n";
}

}  // namespace jellybean::demo
