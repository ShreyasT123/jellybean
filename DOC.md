# jellybean: High-performance Efficient Low-latency Infrastructure for Orchestrated Systems
### A Production-Grade Distributed Actor Runtime in C++20/23

> **Principal Engineer's Note**: This document is your north star for building infrastructure-grade distributed systems. Every design decision is deliberate, every tradeoff is explicit. We build this the way Aeron, Seastar, and ScyllaDB teams build systems — subsystem by subsystem, benchmark by benchmark.

---

## 0. Environment Reality Check (Windows i5-13th Gen Host)

You are on **Windows with WSL2 + Docker**. Here is what that means for this project:

```
Host (Windows 11)
├── WSL2 Ubuntu 22.04  ← primary dev environment, use this always
│   ├── clang-17 / gcc-13
│   ├── perf, bpftrace, valgrind, heaptrack
│   └── CMake + Ninja
└── Docker (Linux containers)
    ├── Single-node dev cluster
    ├── Multi-node k8s simulation (kind)
    └── CI pipeline target (Jenkins)
```

**Critical**: `io_uring`, `epoll`, `NUMA`, `eBPF` — all require running inside WSL2 or Docker Linux containers.
`io_uring` requires Linux kernel ≥ 5.10 (WSL2 typically ships with 5.15+). Verify:

```bash
uname -r          # must be 5.10+
ls /sys/bus/node  # NUMA: WSL2 shows single NUMA node, Docker same
cat /proc/cpuinfo | grep "model name" | head -1
```

Your Intel i5-13th gen has **P-cores + E-cores** (hybrid architecture). This matters enormously for thread pinning — we will handle it.

---

## 1. Project Identity

**Name**: jellybean  
**What it is**: A distributed actor runtime with a custom scheduler, append-only replicated log, lock-free message queues, and a zero-copy binary protocol — all built from scratch in C++20.

**What you will have built after 6 months that impresses MAANG**:
- A Seastar-style per-core reactor loop
- A work-stealing fiber/coroutine scheduler (like Tokio's)
- An Aeron-style lock-free SPSC/MPMC ring buffer
- A Kafka-style append-only log with fsync batching
- A Raft-style distributed consensus layer
- A custom arena + slab allocator
- A binary protocol (like FlatBuffers but hand-rolled)
- eBPF-instrumented hot paths with flamegraph-driven optimization

---

## 2. Phased Roadmap

```
Phase 1 (Weeks 1–3):   Foundation — allocators, ring buffers, core primitives
Phase 2 (Weeks 4–6):   Reactor loop — epoll + io_uring event loop, fiber runtime
Phase 3 (Weeks 7–9):   Actor runtime — mailboxes, scheduling, work stealing
Phase 4 (Weeks 10–13): Network stack — zero-copy, binary protocol, zero-copy I/O
Phase 5 (Weeks 14–17): Distributed log + Raft — replication, leader election
Phase 6 (Weeks 18–22): Cluster runtime — distributed task execution, observability
```

---

## 3. Folder Structure (Production-Style)

```
jellybean/
├── CMakeLists.txt                  # top-level, sets C++23 standard
├── CMakePresets.json               # debug/release/asan/tsan presets
├── vcpkg.json                      # minimal dependencies
├── .clang-format
├── .clang-tidy
├── Dockerfile.dev
├── docker-compose.cluster.yml
│
├── cmake/
│   ├── CompilerOptions.cmake
│   ├── Sanitizers.cmake
│   ├── FindLiburing.cmake
│   └── CPM.cmake                   # header-only dependency manager
│
├── include/jellybean/                 # public headers only
│   ├── core/
│   │   ├── platform.hpp            # CPU detection, arch macros
│   │   ├── compiler.hpp            # LIKELY/UNLIKELY, FORCE_INLINE, NO_INLINE
│   │   ├── types.hpp               # u8, u16, u32, u64, i64, etc.
│   │   └── assert.hpp              # jellybean_assert, contract checks
│   ├── memory/
│   │   ├── arena.hpp
│   │   ├── slab.hpp
│   │   ├── pool.hpp
│   │   ├── huge_pages.hpp
│   │   └── numa.hpp
│   ├── concurrency/
│   │   ├── spsc_queue.hpp          # single producer single consumer
│   │   ├── mpmc_queue.hpp          # multi producer multi consumer
│   │   ├── seqlock.hpp
│   │   ├── hazard_ptr.hpp
│   │   ├── epoch_reclamation.hpp
│   │   └── backoff.hpp
│   ├── scheduler/
│   │   ├── fiber.hpp
│   │   ├── work_queue.hpp
│   │   ├── work_stealing_deque.hpp
│   │   └── scheduler.hpp
│   ├── reactor/
│   │   ├── reactor.hpp             # per-core event loop
│   │   ├── io_uring_backend.hpp
│   │   ├── epoll_backend.hpp
│   │   └── timer_wheel.hpp
│   ├── actor/
│   │   ├── actor.hpp
│   │   ├── actor_ref.hpp
│   │   ├── mailbox.hpp
│   │   └── registry.hpp
│   ├── net/
│   │   ├── socket.hpp
│   │   ├── tcp_connection.hpp
│   │   ├── buffer_pool.hpp         # zero-copy buffer management
│   │   └── scatter_gather.hpp
│   ├── proto/
│   │   ├── wire.hpp                # binary wire format
│   │   ├── codec.hpp
│   │   └── frame.hpp
│   ├── log/
│   │   ├── segment.hpp             # append-only log segment
│   │   ├── log.hpp
│   │   └── index.hpp
│   ├── raft/
│   │   ├── state_machine.hpp
│   │   ├── log_entry.hpp
│   │   ├── raft_node.hpp
│   │   └── rpc.hpp
│   ├── telemetry/
│   │   ├── metrics.hpp             # lock-free counters
│   │   ├── histogram.hpp           # HDR histogram
│   │   └── tracer.hpp
│   └── simd/
│       ├── detect.hpp              # AVX2/SSE4.2 detection
│       └── memops.hpp              # SIMD memcpy, checksum
│
├── src/
│   ├── memory/
│   │   ├── arena.cpp
│   │   ├── slab.cpp
│   │   └── huge_pages.cpp
│   ├── scheduler/
│   │   ├── fiber.cpp               # ucontext / assembly context switch
│   │   └── scheduler.cpp
│   ├── reactor/
│   │   ├── reactor.cpp
│   │   ├── io_uring_backend.cpp
│   │   └── timer_wheel.cpp
│   ├── actor/
│   │   ├── actor.cpp
│   │   └── registry.cpp
│   ├── net/
│   │   ├── tcp_connection.cpp
│   │   └── buffer_pool.cpp
│   ├── proto/
│   │   └── codec.cpp
│   ├── log/
│   │   ├── segment.cpp
│   │   └── log.cpp
│   └── raft/
│       └── raft_node.cpp
│
├── benchmarks/
│   ├── bench_spsc.cpp
│   ├── bench_allocator.cpp
│   ├── bench_scheduler.cpp
│   ├── bench_net_throughput.cpp
│   └── bench_log_append.cpp
│
├── tests/
│   ├── unit/
│   │   ├── test_spsc.cpp
│   │   ├── test_arena.cpp
│   │   └── test_raft_log.cpp
│   └── integration/
│       ├── test_actor_cluster.cpp
│       └── test_raft_election.cpp
│
├── tools/
│   ├── codegen/                    # protocol schema → C++ code
│   ├── flame/                      # flamegraph scripts
│   │   ├── record.sh
│   │   └── flamegraph.sh
│   └── tuning/
│       ├── kernel_tune.sh          # sysctl, IRQ affinity
│       └── cpu_isolate.sh
│
└── docs/
    ├── architecture.md
    ├── memory_model.md
    ├── wire_protocol.md
    └── raft_impl.md
```

---

## 4. Dependencies (Minimal, Intentional)

```json
// vcpkg.json
{
  "name": "jellybean",
  "version-string": "0.1.0",
  "dependencies": [
    "benchmark",        // Google Benchmark
    "gtest",           // Google Test
    "spdlog",          // Structured logging (header-only)
    "fmt"              // fmt::format (C++20 std::format fallback)
  ]
}
```

**Manually vendored** (in `third_party/`):
- `liburing` headers (io_uring) — build from source for latest features
- `HdrHistogram_c` — lock-free latency histogram

**Everything else is hand-rolled.** No Boost. No Abseil. No Folly.  
This is intentional — you learn by building, not by importing.

---

## 5. CMake Architecture

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)
project(jellybean LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Presets: debug, release, asan, tsan, ubsan
include(cmake/CompilerOptions.cmake)
include(cmake/Sanitizers.cmake)

# Core library
add_library(jellybean_core STATIC
    src/memory/arena.cpp
    src/scheduler/fiber.cpp
    src/reactor/reactor.cpp
    # ...
)

target_include_directories(jellybean_core PUBLIC include)
target_compile_options(jellybean_core PRIVATE
    -march=native            # use all CPU features available
    -fno-exceptions          # no exception overhead in hot paths
    -fno-rtti
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
```

```json
// CMakePresets.json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "release",
      "generator": "Ninja",
      "binaryDir": "build/release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS": "-O3 -march=native -DNDEBUG"
      }
    },
    {
      "name": "asan",
      "generator": "Ninja", 
      "binaryDir": "build/asan",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "jellybean_SANITIZER": "address"
      }
    },
    {
      "name": "tsan",
      "binaryDir": "build/tsan",
      "cacheVariables": { "jellybean_SANITIZER": "thread" }
    }
  ]
}
```

---

## 6. Phase 1: Foundation — Memory & Primitives

### 6.1 The Memory Philosophy

> In HFT, distributed DB, and ML serving systems, allocator pressure is frequently the #1 latency killer. `malloc` involves kernel calls, locks, and cache thrashing. We eliminate it from hot paths entirely.

**Three allocators you will build**:

```
┌─────────────────────────────────────────────────────┐
│  ArenaAllocator     — bump pointer, bulk free only  │
│  SlabAllocator      — fixed-size object pools       │
│  HugePageAllocator  — 2MB pages, TLB-friendly       │
└─────────────────────────────────────────────────────┘
```

#### Arena Allocator

```cpp
// include/jellybean/memory/arena.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>
#include "jellybean/core/compiler.hpp"

namespace jellybean::memory {

// Thread-local arena — no synchronization needed
// Inspired by how ScyllaDB manages per-shard memory
class ArenaAllocator {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 2 * 1024 * 1024; // 2MB

    explicit ArenaAllocator(size_t block_size = DEFAULT_BLOCK_SIZE);
    ~ArenaAllocator();

    // Non-copyable, movable
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept;

    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    [[nodiscard]] jellybean_FORCE_INLINE
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // Align current pointer
        uintptr_t cur = reinterpret_cast<uintptr_t>(current_);
        uintptr_t aligned = (cur + alignment - 1) & ~(alignment - 1);
        uintptr_t next = aligned + size;

        if (jellybean_LIKELY(next <= reinterpret_cast<uintptr_t>(end_))) {
            current_ = reinterpret_cast<std::byte*>(next);
            return reinterpret_cast<void*>(aligned);
        }
        return allocate_slow(size, alignment);
    }

    // Reset without freeing — reuse for next request cycle
    void reset() noexcept;

    // Statistics
    size_t bytes_allocated() const noexcept;
    size_t bytes_wasted() const noexcept;  // alignment padding

private:
    struct Block {
        Block* next;
        size_t size;
        std::byte data[];   // flexible array member
    };

    void* allocate_slow(size_t size, size_t alignment);
    Block* allocate_block(size_t min_size);

    std::byte* current_{nullptr};
    std::byte* end_{nullptr};
    Block* head_{nullptr};
    size_t block_size_;
    size_t total_allocated_{0};
};

} // namespace jellybean::memory
```

#### Slab Allocator (for fixed-size actor mailbox entries)

```cpp
// include/jellybean/memory/slab.hpp
// Inspired by Linux kernel slab allocator and jemalloc's size classes

template<typename T, size_t ChunkObjects = 64>
class SlabAllocator {
    // Each "slab" is a contiguous block holding ChunkObjects T instances
    // Free list is intrusive — uses the object memory itself when free
    // O(1) alloc/free, cache-line friendly layout
    struct FreeNode { FreeNode* next; };
    static_assert(sizeof(T) >= sizeof(FreeNode*), "T too small for slab");
    // ...
};
```

#### Huge Pages (Critical for NUMA + TLB performance)

```cpp
// include/jellybean/memory/huge_pages.hpp
// 2MB huge pages eliminate TLB misses on large buffers
// Used for: ring buffers, log segments, network receive buffers

void* allocate_huge_pages(size_t size_bytes);
// Uses mmap(MAP_HUGETLB | MAP_HUGE_2MB) on Linux
// Falls back to mmap(MAP_ANONYMOUS) if huge pages unavailable
```

**Enable huge pages in your environment**:
```bash
# In WSL2 / Docker
echo 512 | sudo tee /proc/sys/vm/nr_hugepages
grep HugePages /proc/meminfo
```

### 6.2 Lock-Free Ring Buffer (SPSC)

This is the **most critical primitive** in the entire system. Everything rides on it.

```cpp
// include/jellybean/concurrency/spsc_queue.hpp
// 
// Design: Aeron-style SPSC ring buffer
// - Single producer, single consumer
// - Power-of-2 size for cheap modulo (bitmask)
// - Separate cache lines for head/tail — eliminates false sharing
// - Acquire/release memory ordering — no unnecessary fences
// - Works for actor-to-actor messaging on same node
//
// Compared to Boost.Lockfree: no Boost dep, better cache layout
// Compared to folly::ProducerConsumerQueue: no exception overhead

#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include "jellybean/core/compiler.hpp"

namespace jellybean::concurrency {

template<typename T, size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)  // power of 2
class SpscQueue {
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    // CRITICAL: Each atomic on its own cache line
    // Without this, producer and consumer fight over the same cache line
    // — this is "false sharing" and destroys performance
    alignas(CACHE_LINE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE) std::array<T, Capacity> buffer_;

public:
    // Called by producer thread ONLY
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t next = t + 1;
        // Check if full — only read head with acquire if we think we're full
        if (next - head_.load(std::memory_order_acquire) > Capacity) {
            return false;
        }
        buffer_[t & MASK] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Called by consumer thread ONLY
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T item = buffer_[h & MASK];
        head_.store(h + 1, std::memory_order_release);
        return item;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) -
               head_.load(std::memory_order_acquire);
    }
};

} // namespace jellybean::concurrency
```

**Memory Ordering Lesson** (this is what MAANG interviews drill):

```
Relaxed  — no ordering guarantees, just atomicity
Acquire  — no reads/writes can be reordered BEFORE this load
Release  — no reads/writes can be reordered AFTER this store
AcqRel   — both
SeqCst   — total order across all threads (expensive on ARM!)

In SPSC queue:
- tail_.store(release)  ensures buffer write is visible before tail update
- tail_.load(acquire)   ensures consumer sees buffer write after seeing new tail
- head_.load(relaxed)   producer's own view of head — no sync needed
```

### 6.3 MPMC Queue (for work stealing)

```cpp
// include/jellybean/concurrency/mpmc_queue.hpp
// Dmitry Vyukov's classic MPMC — used in Tokio, Folly, Intel TBB
// Each slot has a sequence counter — avoids ABA problem entirely
// CachePad between slots if T is small

template<typename T, size_t Capacity>
class MpmcQueue {
    struct Slot {
        std::atomic<size_t> sequence;
        T data;
    };
    alignas(64) std::array<Slot, Capacity> slots_;
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
    // ...
};
```

### 6.4 Seqlock (for read-heavy shared config)

```cpp
// Used for: cluster membership, routing tables, config snapshots
// Writers are rare, readers are frequent — perfect for seqlock
// No reader starvation, no lock, no atomic RMW on read path

class Seqlock {
    std::atomic<uint64_t> seq_{0};
public:
    template<typename F>
    void write(F&& fn) {
        uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);  // odd = writing
        std::atomic_signal_fence(std::memory_order_acq_rel);
        fn();
        std::atomic_signal_fence(std::memory_order_acq_rel);
        seq_.store(s + 2, std::memory_order_release);  // even = done
    }

    template<typename T, typename F>
    T read(F&& fn) {
        while (true) {
            uint64_t s = seq_.load(std::memory_order_acquire);
            if (s & 1) { cpu_relax(); continue; }  // writer active
            T result = fn();
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s) return result;
            // seq changed during read — retry
        }
    }
};
```

---

## 7. Phase 2: Reactor Loop (Event-Driven Core)

> Every modern high-performance server — Nginx, Redis, Seastar, Node.js — is built around an event loop. We build ours per-core, Seastar-style.

### 7.1 Architecture: Per-Core Reactor

```
Core 0                    Core 1                    Core 2
┌─────────────────────┐   ┌─────────────────────┐   ┌─────────────────────┐
│  Reactor (shard 0)  │   │  Reactor (shard 1)  │   │  Reactor (shard 2)  │
│  ┌───────────────┐  │   │  ┌───────────────┐  │   │  ┌───────────────┐  │
│  │ io_uring ring │  │   │  │ io_uring ring │  │   │  │ io_uring ring │  │
│  │ (SQ/CQ pair)  │  │   │  │ (SQ/CQ pair)  │  │   │  │ (SQ/CQ pair)  │  │
│  └───────────────┘  │   │  └───────────────┘  │   │  └───────────────┘  │
│  ┌───────────────┐  │   │  ┌───────────────┐  │   │  ┌───────────────┐  │
│  │  Timer Wheel  │  │   │  │  Timer Wheel  │  │   │  │  Timer Wheel  │  │
│  └───────────────┘  │   │  └───────────────┘  │   │  └───────────────┘  │
│  ┌───────────────┐  │   │  ┌───────────────┐  │   │  ┌───────────────┐  │
│  │ Fiber Sched.  │  │   │  │ Fiber Sched.  │  │   │  │ Fiber Sched.  │  │
│  └───────────────┘  │   │  └───────────────┘  │   │  └───────────────┘  │
└─────────────────────┘   └─────────────────────┘   └─────────────────────┘
         ▲                           ▲                          ▲
    CPU pinned                  CPU pinned                 CPU pinned
    (sched_setaffinity)         (sched_setaffinity)        (sched_setaffinity)
```

**Key insight vs Tokio**: Tokio uses work-stealing across threads. Seastar/jellybean uses *sharding* — each core owns its data and actors. Cross-shard communication is explicit (message passing). This eliminates almost all synchronization overhead and is how ScyllaDB achieves its performance.

### 7.2 io_uring Backend

```cpp
// src/reactor/io_uring_backend.cpp
//
// io_uring operates on two ring buffers shared between kernel and userspace:
//   SQ (Submission Queue) — userspace writes I/O requests here
//   CQ (Completion Queue) — kernel writes completions here
//
// Key advantage over epoll:
//   epoll: syscall per event registration + syscall per wait
//   io_uring: batch N operations in one syscall (io_uring_submit)
//             kernel can also poll without any syscall (SQPOLL mode)

#include <liburing.h>

class IoUringBackend {
    struct io_uring ring_;
    static constexpr unsigned QUEUE_DEPTH = 4096;

public:
    IoUringBackend() {
        struct io_uring_params params{};
        // IORING_SETUP_SQPOLL: kernel thread polls SQ — zero syscall I/O
        // Requires CAP_SYS_NICE or recent kernel. Fallback if fails.
        params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
        params.sq_thread_cpu = cpu_id_;  // pin kernel thread to same CPU

        int ret = io_uring_queue_init_params(QUEUE_DEPTH, &ring_, &params);
        if (ret < 0) {
            // Fallback: no SQPOLL
            io_uring_queue_init(QUEUE_DEPTH, &ring_, 0);
        }
    }

    // Submit an async read — returns immediately, completion arrives later
    void submit_read(int fd, void* buf, size_t len, off_t offset, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_read(sqe, fd, buf, len, offset);
        io_uring_sqe_set_data(sqe, user_data);
    }

    // Drain completions — call this in your reactor loop tick
    int drain_completions(CompletionCallback cb) {
        struct io_uring_cqe* cqe;
        unsigned head;
        int count = 0;

        io_uring_for_each_cqe(&ring_, head, cqe) {
            cb(io_uring_cqe_get_data(cqe), cqe->res);
            ++count;
        }
        io_uring_cq_advance(&ring_, count);
        return count;
    }

    void submit() { io_uring_submit(&ring_); }
};
```

### 7.3 Timer Wheel

```cpp
// Hashed timer wheel — O(1) insert, O(1) expire
// Used by: Kafka, Netty, Linux kernel (for TCP timers)
// Much better than heap-based timers for high-timer-count systems

class TimerWheel {
    static constexpr size_t SLOTS = 512;        // power of 2
    static constexpr uint64_t TICK_NS = 1'000'000; // 1ms per tick

    using TimerList = std::vector<TimerEntry>;
    std::array<TimerList, SLOTS> slots_;
    uint64_t current_tick_{0};

    void advance(uint64_t now_ns) {
        uint64_t ticks = (now_ns / TICK_NS) - current_tick_;
        for (uint64_t i = 0; i < ticks; ++i) {
            size_t slot = (current_tick_ + i) & (SLOTS - 1);
            for (auto& entry : slots_[slot]) {
                if (entry.expires_tick == current_tick_ + i) {
                    entry.callback();
                }
            }
            slots_[slot].clear();
        }
        current_tick_ += ticks;
    }
};
```

### 7.4 Fiber Runtime

> Fibers are **user-space cooperative threads**. They allow writing async code in a sequential style. Tokio does this with Rust's async/await. We do it with C++20 coroutines or manual ucontext switching.

**Two options**:
1. **C++20 coroutines** — `co_await`, `co_return`, compiler-managed frame. More ergonomic.
2. **ucontext/assembly** — manual stack switching. Faster, more control. Used by Seastar (via Boost.Context) and Facebook's Folly.

**Start with C++20 coroutines** (Phase 2), then optimize hot paths with manual fiber switching (Phase 3+).

```cpp
// include/jellybean/scheduler/fiber.hpp
// C++20 coroutine-based fiber

#include <coroutine>

// Awaitable that yields control back to the reactor
struct Yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // Re-queue this coroutine handle in the current reactor's run queue
        current_reactor()->schedule(h);
    }
    void await_resume() noexcept {}
};

// Task<T> — the coroutine return type for jellybean fibers
template<typename T = void>
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
    };
    std::coroutine_handle<promise_type> handle;
};

// Usage:
// Task<> my_actor_handler() {
//     auto data = co_await recv_from_socket(fd);
//     co_await Yield{};  // give up CPU, let others run
//     co_await send_to_socket(fd, data);
// }
```

---

## 8. Phase 3: Actor Runtime

### 8.1 Actor Model Design

```
Design choices compared to alternatives:

Erlang/OTP:    Process-based, heap-per-actor, GC per actor
Akka (JVM):    Thread pool + mailbox, JVM GC pauses
Ray:           Python-first, object store, GIL issues
jellybean:        C++ coroutine fiber per actor, arena-allocated mailbox,
               zero-copy message passing (shared_ptr to immutable buffers)
               Shard-local actors need zero synchronization
               Cross-shard actors use SPSC queue per pair
```

```cpp
// include/jellybean/actor/actor.hpp
#pragma once
#include "jellybean/scheduler/fiber.hpp"
#include "jellybean/concurrency/spsc_queue.hpp"
#include "jellybean/memory/arena.hpp"

namespace jellybean::actor {

class ActorBase {
public:
    using ActorId = uint64_t;
    
    virtual ~ActorBase() = default;
    virtual Task<> receive(Message msg) = 0;
    
    ActorId id() const noexcept { return id_; }
    uint32_t shard() const noexcept { return shard_id_; }

protected:
    ActorId id_;
    uint32_t shard_id_;
    ArenaAllocator* arena_;  // shard-local, no synchronization
};

// Type-erased message — zero allocation for small messages (SSO-style)
struct Message {
    static constexpr size_t INLINE_SIZE = 48;
    
    uint32_t type_id;
    uint32_t sender_shard;
    uint64_t sender_id;
    
    union {
        std::byte inline_data[INLINE_SIZE];
        struct { void* ptr; size_t size; } heap_data;
    };
    bool is_inline;
    
    template<typename T>
    const T& as() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if constexpr (sizeof(T) <= INLINE_SIZE) {
            return *reinterpret_cast<const T*>(inline_data);
        }
        return *static_cast<const T*>(heap_data.ptr);
    }
};

} // namespace jellybean::actor
```

### 8.2 Work-Stealing Scheduler

```
This is the heart of the runtime. Compared to:
  Tokio:    Chase-Lev deque, per-thread local queue + global queue
  Go:       Similar, but with goroutine scheduler preemption
  Seastar:  No stealing — shard-local only
  jellybean:   Chase-Lev deque per shard, optional cross-shard steal
            (disable steal by default for Seastar-style isolation)
```

```cpp
// include/jellybean/scheduler/work_stealing_deque.hpp
// Chase-Lev dynamic array deque (2005 paper: "Dynamic Circular Work-Stealing Deque")
// - push/pop on "bottom" by owning thread — no sync
// - steal from "top" by other threads — one CAS

template<typename T>
class WorkStealingDeque {
    struct Array {
        std::atomic<T*> storage;
        int64_t capacity;
        int64_t mask() const { return capacity - 1; }
        T load(int64_t i) { return storage.load(std::memory_order_relaxed)[i & mask()]; }
        void store(int64_t i, T val) { storage.load(std::memory_order_relaxed)[i & mask()] = val; }
    };

    alignas(64) std::atomic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    std::atomic<Array*> array_;

public:
    void push(T item);            // owner thread only
    std::optional<T> pop();       // owner thread only
    std::optional<T> steal();     // any thread — uses CAS
};
```

### 8.3 Thread Pinning + CPU Topology

```cpp
// src/scheduler/scheduler.cpp

// i5-13th gen hybrid: P-cores (high perf) + E-cores (efficiency)
// Parse /sys/devices/system/cpu/cpu*/topology/core_id to distinguish

struct CpuTopology {
    struct Core {
        int cpu_id;       // logical CPU index
        int physical_id;  // physical core
        bool is_p_core;   // performance vs efficiency
        int numa_node;
    };
    std::vector<Core> cores;
    
    static CpuTopology detect() {
        // Read /proc/cpuinfo and /sys/devices/system/cpu/
        // On i5-13th gen: CPUs 0-7 are P-cores, 8-11 are E-cores
        // Pin reactor threads to P-cores only
    }
};

void pin_thread_to_cpu(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// Set thread priority (requires permissions in Docker/WSL2)
void set_thread_realtime(int priority = 50) {
    struct sched_param param{.sched_priority = priority};
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
}
```

---

## 9. Phase 4: Network Stack + Binary Protocol

### 9.1 Zero-Copy Networking Design

```
Zero-copy means: data moves from kernel network buffer → userspace buffer → socket
                 WITHOUT copying through intermediate CPU buffers.

Techniques:
1. recv/send with MSG_ZEROCOPY (Linux 4.14+) — kernel notifies when done
2. io_uring + fixed buffers — pre-registered buffers, no copy on recv
3. scatter-gather I/O (readv/writev) — multiple buffers in one syscall
4. splice/sendfile — kernel-to-kernel copy bypass

We implement: io_uring fixed buffers + scatter-gather
```

```cpp
// include/jellybean/net/buffer_pool.hpp
// Pre-allocated, fixed-size buffer pool
// Registered with io_uring for zero-copy recv

class BufferPool {
    static constexpr size_t BUFFER_SIZE = 4096;   // one page
    static constexpr size_t POOL_SIZE   = 1024;   // 4MB total

    struct Buffer {
        alignas(64) std::byte data[BUFFER_SIZE];
        std::atomic<bool> in_use{false};
        uint16_t pool_index;
    };

    std::unique_ptr<Buffer[]> buffers_;  // allocated with huge pages
    struct iovec iovecs_[POOL_SIZE];     // registered with io_uring

public:
    BufferPool() {
        // Allocate using huge pages (2MB) for TLB efficiency
        buffers_ = allocate_huge_pages_unique<Buffer>(POOL_SIZE);
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            iovecs_[i] = { buffers_[i].data, BUFFER_SIZE };
            buffers_[i].pool_index = static_cast<uint16_t>(i);
        }
    }

    void register_with_ring(struct io_uring* ring) {
        io_uring_register_buffers(ring, iovecs_, POOL_SIZE);
    }

    Buffer* acquire() noexcept {
        // Lock-free scan — in practice use a free-list SPSC queue
        for (auto& buf : std::span(buffers_.get(), POOL_SIZE)) {
            bool expected = false;
            if (buf.in_use.compare_exchange_weak(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return &buf;
            }
        }
        return nullptr;
    }

    void release(Buffer* buf) noexcept {
        buf->in_use.store(false, std::memory_order_release);
    }
};
```

### 9.2 Binary Wire Protocol Design

```
Design principles (inspired by Cap'n Proto, Aeron, SBE):
- No schema encoding in wire format (fixed layout known at compile time)
- No dynamic memory allocation during parse
- Field access is pointer arithmetic — O(1), no deserialization
- Version field in header for forward compatibility
- Little-endian (x86 native — avoids bswap on every field read)
- Checksum on every frame (CRC32C via SSE4.2 PCRC32 instruction)
```

```
Wire Frame Layout (64-byte aligned):
┌────────────────────────────────────────────────────────────────┐
│  0       4       8      12      16      24      32      64     │
│  magic   version type    flags   length  actor   message  ...  │
│  4B      2B      2B      2B      4B      8B      8B       pad  │
│  0xHEL1  u16     u16     u16     u32     u64     u64      ...  │
└────────────────────────────────────────────────────────────────┘
│  payload (variable, padded to 8B boundary)                     │
│  crc32c (4B, covers header + payload, at end)                  │
└────────────────────────────────────────────────────────────────┘
```

```cpp
// include/jellybean/proto/wire.hpp
#pragma once
#include <cstdint>
#include <span>
#include "jellybean/simd/detect.hpp"

namespace jellybean::proto {

static constexpr uint32_t MAGIC    = 0x48454C31;  // "HEL1"
static constexpr uint16_t VERSION  = 1;

enum class MessageType : uint16_t {
    Handshake    = 0x0001,
    ActorMessage = 0x0002,
    Heartbeat    = 0x0003,
    RaftAppend   = 0x0010,
    RaftVote     = 0x0011,
    RaftSnapshot = 0x0012,
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint16_t flags;
    uint16_t _pad;
    uint32_t payload_length;
    uint64_t actor_id;
    uint64_t message_id;
    // Followed by: payload[payload_length], then uint32_t crc32c
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 32);

// CRC32C using SSE4.2 — hardware instruction, ~4 bytes/cycle
uint32_t crc32c(std::span<const std::byte> data) noexcept;

// Zero-copy parse — no allocation, returns view into existing buffer
struct ParsedFrame {
    const FrameHeader* header;     // points into receive buffer
    std::span<const std::byte> payload;
    bool valid;
};

ParsedFrame parse_frame(std::span<const std::byte> buf) noexcept;

// Encode — writes directly into pre-allocated buffer
size_t encode_frame(std::span<std::byte> buf, MessageType type,
                    uint64_t actor_id, std::span<const std::byte> payload) noexcept;

} // namespace jellybean::proto
```

### 9.3 SIMD CRC32C

```cpp
// src/proto/codec.cpp — SIMD-accelerated CRC32C

#if defined(__SSE4_2__)
#include <nmmintrin.h>

uint32_t crc32c(std::span<const std::byte> data) noexcept {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    size_t len = data.size();

    // Process 8 bytes at a time with 64-bit CRC32C instruction
    while (len >= 8) {
        crc = static_cast<uint32_t>(
            _mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(p)));
        p += 8; len -= 8;
    }
    // Handle remaining bytes
    while (len--) crc = _mm_crc32_u8(crc, *p++);
    return ~crc;
}
#endif
```

---

## 10. Phase 5: Append-Only Log + Raft Consensus

### 10.1 Log Design (Kafka-Inspired)

```
Architecture:
  Log = sequence of Segments
  Segment = memory-mapped file + offset index
  Index   = sparse: every Nth entry has {offset, file_position}

Kafka does this. ClickHouse does this for MergeTree parts.
ScyllaDB does this for commitlog.

Key properties:
  Sequential writes only — maximizes disk bandwidth
  mmap read path — OS page cache is your buffer pool
  fsync batched — group commit, not per-message
  Segments roll over at 1GB — compaction-friendly
```

```cpp
// include/jellybean/log/segment.hpp

class LogSegment {
    int fd_;
    void* mmap_ptr_;          // read path uses mmap
    size_t mmap_size_;
    std::atomic<size_t> write_pos_{0};
    uint64_t base_offset_;    // first log offset in this segment

public:
    // Append — O(1), sequential write
    // Returns the log offset of the appended entry
    [[nodiscard]] uint64_t append(std::span<const std::byte> data);

    // Read — zero-copy via mmap
    // Returns a view into mapped memory — NO copying
    [[nodiscard]] std::span<const std::byte>
    read(uint64_t offset, size_t max_bytes) const noexcept;

    // Group commit — batch fsync for durability
    void fsync() noexcept;

    // For Raft: truncate uncommitted tail
    void truncate(uint64_t new_end_offset);
};
```

### 10.2 Raft Implementation

```
Raft roles: Leader, Follower, Candidate
Leader election: randomized timeouts, majority vote
Log replication: leader sends AppendEntries RPCs, commits when majority acks
Safety: no two leaders in same term, logs match at commit index
```

```cpp
// include/jellybean/raft/raft_node.hpp

enum class RaftRole { Follower, Candidate, Leader };

struct LogEntry {
    uint64_t index;
    uint64_t term;
    std::vector<std::byte> data;
};

class RaftNode {
    // Persistent state (survive crashes)
    std::atomic<uint64_t> current_term_{0};
    std::optional<uint32_t> voted_for_;
    Log log_;

    // Volatile state
    RaftRole role_{RaftRole::Follower};
    uint64_t commit_index_{0};
    uint64_t last_applied_{0};
    uint32_t leader_id_{0};

    // Leader-only volatile state
    std::vector<uint64_t> next_index_;   // next log index to send to each peer
    std::vector<uint64_t> match_index_;  // highest confirmed match per peer

    // Timeouts (randomized to avoid split votes)
    std::chrono::milliseconds election_timeout_;  // 150-300ms
    std::chrono::milliseconds heartbeat_interval_{50ms};

public:
    // Called when AppendEntries RPC arrives from leader
    AppendEntriesReply on_append_entries(const AppendEntriesRequest& req);

    // Called when RequestVote RPC arrives from candidate
    RequestVoteReply on_request_vote(const RequestVoteRequest& req);

    // Client propose (leader only) — returns future for commit
    Task<uint64_t> propose(std::span<const std::byte> data);

    // Tick — called periodically by reactor timer
    void tick();
};
```

---

## 11. Phase 6: Observability, Telemetry, eBPF

### 11.1 Lock-Free Metrics

```cpp
// include/jellybean/telemetry/metrics.hpp
// Per-shard counters — no synchronization
// Aggregated on read by summing across shards

struct ShardMetrics {
    alignas(64) std::atomic<uint64_t> messages_sent{0};
    alignas(64) std::atomic<uint64_t> messages_dropped{0};
    alignas(64) std::atomic<uint64_t> bytes_written{0};
    alignas(64) std::atomic<uint64_t> scheduler_steals{0};
    alignas(64) std::atomic<uint64_t> io_completions{0};
};

// HDR Histogram — accurate latency percentiles without floating point error
// Used by: Kafka, Aeron, HdrHistogram project
class LatencyHistogram {
    std::array<std::atomic<uint64_t>, 1024> buckets_{};

    static size_t bucket_for(uint64_t latency_ns) noexcept {
        // Log-scale bucketing
        if (latency_ns == 0) return 0;
        return std::min(std::bit_width(latency_ns), size_t{1023});
    }

public:
    void record(uint64_t latency_ns) noexcept {
        buckets_[bucket_for(latency_ns)].fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t percentile(double p) const noexcept; // p in [0, 1]
    uint64_t p50() const { return percentile(0.50); }
    uint64_t p99() const { return percentile(0.99); }
    uint64_t p999() const { return percentile(0.999); }
};
```

---

## 12. Performance Engineering Workflow

### 12.1 Profiling Stack

```
Layer 1: Google Benchmark    — micro-benchmarks, ns-level
Layer 2: perf stat           — CPU counters (cache misses, branch mispredicts, IPC)
Layer 3: perf record + FlameGraph — where CPU time goes
Layer 4: bpftrace / eBPF    — kernel-level events, zero overhead probes
Layer 5: Valgrind cachegrind — cache simulation
Layer 6: Intel VTune / AMD uprof — hardware PMU events (needs real hardware)
```

### 12.2 Flamegraph Workflow

```bash
# tools/flame/record.sh
#!/bin/bash
BINARY=$1
DURATION=${2:-30}

# Record with all events
perf record -g -F 997 --call-graph dwarf -p $(pgrep $BINARY) -- sleep $DURATION

# Generate flamegraph
perf script | stackcollapse-perf.pl | flamegraph.pl \
    --title "jellybean $(date)" \
    --colors hot \
    --width 1800 \
    > flamegraph_$(date +%Y%m%d_%H%M%S).svg

echo "Open in browser: flamegraph_*.svg"
```

### 12.3 eBPF Instrumentation

```bash
# Track scheduler latency — time from task wakeup to execution
# tools/ebpf/sched_latency.bt
bpftrace -e '
tracepoint:sched:sched_wakeup /comm == "jellybean-worker"/ {
    @start[tid] = nsecs;
}
tracepoint:sched:sched_switch /comm == "jellybean-worker"/ {
    if (@start[tid]) {
        @latency = hist(nsecs - @start[tid]);
        delete(@start[tid]);
    }
}
END { print(@latency); }'

# Track io_uring completion latency
bpftrace -e '
tracepoint:io_uring:io_uring_submit_sqe { @submit[args->user_data] = nsecs; }
tracepoint:io_uring:io_uring_complete {
    if (@submit[args->user_data]) {
        @uring_lat = hist(nsecs - @submit[args->user_data]);
        delete(@submit[args->user_data]);
    }
}
END { print(@uring_lat); }'
```

### 12.4 Hardware Performance Counters

```bash
# CPU cache behavior — critical for our cache-aware design
perf stat -e \
    cache-misses,cache-references,\
    L1-dcache-load-misses,L1-dcache-loads,\
    LLC-load-misses,LLC-loads,\
    branch-misses,branches,\
    instructions,cycles \
    ./build/release/benchmarks/bench_spsc

# Expected on healthy SPSC benchmark:
#   L1-dcache-load-misses: <1%  (we own both cache lines)
#   LLC-load-misses: near 0
#   IPC: 3-4 on modern Intel (good instruction-level parallelism)
```

### 12.5 Sanitizer Matrix

```cmake
# cmake/Sanitizers.cmake
if(jellybean_SANITIZER STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(jellybean_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(jellybean_SANITIZER STREQUAL "memory")
    add_compile_options(-fsanitize=memory -fno-omit-frame-pointer)
    add_link_options(-fsanitize=memory)
endif()
```

```bash
# Run sequence for every PR:
cmake --preset asan   && ninja -C build/asan   && ctest --preset asan
cmake --preset tsan   && ninja -C build/tsan   && ctest --preset tsan
cmake --preset ubsan  && ninja -C build/ubsan  && ctest --preset ubsan

# TSan catches data races — run with actual concurrent workloads
./build/tsan/tests/integration/test_actor_cluster --threads=8 --duration=60s
```

---

## 13. Linux Kernel Tuning

> These settings belong in `tools/tuning/kernel_tune.sh`. Apply in Docker entrypoint or WSL2 session.

```bash
#!/bin/bash
# Kernel tuning for jellybean — matches production HFT/HPC setups

# ── Network ─────────────────────────────────────────────────────────────
# Increase socket buffer sizes
sysctl -w net.core.rmem_max=134217728      # 128MB receive buffer
sysctl -w net.core.wmem_max=134217728
sysctl -w net.core.rmem_default=16777216
sysctl -w net.core.netdev_max_backlog=5000
sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"

# Reduce TCP latency
sysctl -w net.ipv4.tcp_nodelay=1           # disable Nagle (we do our own batching)
sysctl -w net.ipv4.tcp_low_latency=1
sysctl -w net.ipv4.tcp_congestion_control=bbr  # BBR for datacenter

# ── Memory ──────────────────────────────────────────────────────────────
# Huge pages (for buffer pool, log segments)
echo 1024 > /proc/sys/vm/nr_hugepages     # 2GB of 2MB huge pages
sysctl -w vm.swappiness=0                 # never swap
sysctl -w vm.dirty_ratio=10              # write to disk at 10% dirty pages
sysctl -w vm.dirty_background_ratio=5

# ── CPU ─────────────────────────────────────────────────────────────────
# Disable CPU frequency scaling (for consistent benchmarks)
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" > $cpu 2>/dev/null || true
done

# Disable CPU C-states (latency spikes from deep sleep)
# Note: limited in WSL2/Docker, do this on bare metal
latency-performance tuned profile (if tuned installed):
# tuned-adm profile latency-performance

# ── Scheduler ───────────────────────────────────────────────────────────
# Set kernel preemption for realtime (requires custom kernel or cgroup)
sysctl -w kernel.sched_min_granularity_ns=1000000   # 1ms min timeslice
sysctl -w kernel.sched_wakeup_granularity_ns=500000

# ── I/O ─────────────────────────────────────────────────────────────────
# Use deadline/mq-deadline scheduler for SSDs (not cfq)
for disk in /sys/block/sd*; do
    echo "mq-deadline" > $disk/queue/scheduler 2>/dev/null || true
done

# IRQ affinity — move NIC interrupts to specific cores
# (only relevant with physical NIC, not in WSL2/Docker)
# tools/tuning/irq_balance.sh

echo "Kernel tuning applied."
```

---

## 14. Benchmarking Methodology

### 14.1 Micro-benchmark Template

```cpp
// benchmarks/bench_spsc.cpp
#include <benchmark/benchmark.h>
#include "jellybean/concurrency/spsc_queue.hpp"

static void BM_SpscPushPop(benchmark::State& state) {
    jellybean::concurrency::SpscQueue<uint64_t, 4096> q;
    uint64_t val = 42;

    for (auto _ : state) {
        // Simulate producer-consumer pair
        q.try_push(val);
        auto r = q.try_pop();
        benchmark::DoNotOptimize(r);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * sizeof(uint64_t));
}

BENCHMARK(BM_SpscPushPop)
    ->ThreadRange(1, 2)      // test 1 thread (baseline) and 2 threads
    ->MinWarmUpTime(0.5)
    ->MinTime(2.0)           // run for at least 2 seconds
    ->ReportAggregatesOnly(true);

// Throughput benchmark — max messages/sec
static void BM_SpscThroughput(benchmark::State& state) {
    jellybean::concurrency::SpscQueue<uint64_t, 65536> q;
    std::atomic<bool> running{true};

    // Producer thread
    std::thread producer([&] {
        uint64_t i = 0;
        while (running.load(std::memory_order_relaxed)) {
            while (!q.try_push(i)) { /* spin */ }
            ++i;
        }
    });

    uint64_t consumed = 0;
    for (auto _ : state) {
        if (auto v = q.try_pop()) ++consumed;
    }

    running = false;
    producer.join();
    state.SetItemsProcessed(consumed);
}

BENCHMARK_MAIN();
```

```bash
# Run with CPU pinning for reproducible results
taskset -c 0,1 ./build/release/benchmarks/bench_spsc \
    --benchmark_filter=BM_SpscPushPop \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true \
    --benchmark_format=json \
    --benchmark_out=results/spsc_$(date +%Y%m%d).json
```

### 14.2 End-to-End Latency Benchmark (Most Important)

```
Target metrics (inspired by Aeron, Chronicle, Disruptor benchmarks):
  SPSC queue:        < 50ns round-trip (producer → consumer)
  Actor message:     < 200ns same-shard
  Cross-shard msg:   < 500ns
  Network round-trip: < 50µs (loopback, io_uring)
  Log append + fsync: < 100µs (batch of 1000)
  Raft commit:        < 1ms (3-node, same machine)
```

```cpp
// benchmarks/bench_latency.cpp — coordinated omission-aware latency test
// Uses timestamp injection — measure true latency, not throughput-limited

void latency_benchmark() {
    LatencyHistogram hist;
    const int WARMUP = 100'000;
    const int SAMPLES = 1'000'000;

    // Warmup
    for (int i = 0; i < WARMUP; ++i) {
        send_message_and_wait();
    }

    // Measure
    for (int i = 0; i < SAMPLES; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        send_message_and_wait();
        auto t1 = std::chrono::high_resolution_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    fmt::print("p50:  {:>8} ns\n", hist.p50());
    fmt::print("p99:  {:>8} ns\n", hist.p99());
    fmt::print("p999: {:>8} ns\n", hist.p999());
}
```

---

## 15. Design Decisions vs Prior Art

### Memory Model

| System | Approach | Tradeoff |
|--------|----------|----------|
| **Seastar** | Per-shard allocator, no cross-shard sharing | Zero synchronization, complex ownership |
| **Tokio** | Global allocator (jemalloc), Arc<T> for sharing | Simpler ownership, ref-count overhead |
| **Aeron** | Off-heap `mmap` buffers, no GC | Zero GC, manual lifetime |
| **Kafka** | JVM heap, relies on OS page cache | Simple dev, GC pauses |
| **jellybean** | Arena per shard + slab for fixed objects, huge pages for I/O | Best of Seastar, simpler than Aeron |

### Concurrency Strategy

| System | Model | Notes |
|--------|-------|-------|
| **Seastar** | Shard-local (no cross-thread data) | Zero locks, but complex APIs |
| **Tokio** | Work-stealing across threads | Simpler programming, some overhead |
| **Go runtime** | M:N goroutines with work stealing | GC pauses, great ergonomics |
| **NATS** | Goroutine per connection | Simple, OK perf |
| **jellybean Phase 1** | Shard-local (Seastar model) | Build Tokio-style stealing later |

### Network I/O

| System | I/O Model |
|--------|-----------|
| **Seastar** | DPDK (kernel bypass!) or io_uring |
| **Aeron** | `aeron-driver` process, IPC ring buffer |
| **ScyllaDB** | DPDK in production |
| **Kafka** | Java NIO (epoll under the hood) |
| **jellybean** | io_uring (no DPDK initially), SQPOLL for near-zero syscall overhead |

> **Why not DPDK?** DPDK requires dedicated NICs, special drivers, huge pages, and root access. Great for HFT production, but overkill for a learning project. `io_uring` with SQPOLL gets you 80% of the way there without the complexity.

### Consensus Protocol

| System | Protocol |
|--------|----------|
| **etcd** | Raft |
| **Kafka** | KRaft (custom Raft), previously ZooKeeper |
| **ScyllaDB** | Raft for schema, Paxos for data |
| **Ray** | GCS (Global Control Store, custom) |
| **jellybean** | Raft (simpler, well-understood, good papers) |

---

## 16. Testing Methodology

### 16.1 Test Pyramid

```
                    ┌────────────────┐
                    │  Chaos Tests   │  ← fault injection, network partition
                    │   (few, slow)  │
                 ┌──┴────────────────┴──┐
                 │  Integration Tests   │  ← multi-node Raft, actor cluster
                 │    (moderate)        │
              ┌──┴──────────────────────┴──┐
              │       Unit Tests           │  ← each subsystem in isolation
              │    (many, fast, parallel)  │
           ┌──┴────────────────────────────┴──┐
           │      Property-Based Tests        │  ← rapidcheck, fuzz testing
           │  (invariant checking, RAFT props) │
        ┌──┴──────────────────────────────────┴──┐
        │         Sanitizer Runs                  │  ← ASan, TSan, UBSan
        └─────────────────────────────────────────┘
```

### 16.2 Raft Correctness Testing

```
Raft has known invariants that MUST hold:
1. Election safety: at most one leader per term
2. Leader append-only: leader never overwrites/deletes its log
3. Log matching: same index+term → same entry on all nodes
4. Leader completeness: committed entries present in future leaders
5. State machine safety: same log entry → same state machine output

Test technique: TLA+ spec for Raft exists (Diego Ongaro's original)
For C++: use deterministic simulation testing (DST)
  - Control wall clock, network delay, disk I/O
  - Inject failures at precise points
  - Replay same scenario deterministically
  - Used by FoundationDB, TigerBeetle
```

```cpp
// tests/unit/test_raft_log.cpp
TEST(RaftLog, LeaderAppendOnlyInvariant) {
    SimulatedCluster cluster(3);
    cluster.elect_leader();
    
    auto [leader, term] = cluster.current_leader();
    uint64_t prev_log_len = leader->log_length();
    
    // Propose 100 entries
    for (int i = 0; i < 100; ++i) {
        leader->propose(make_test_entry(i));
        cluster.tick(10ms);
    }
    
    // Leader log must be strictly monotonically growing
    EXPECT_GT(leader->log_length(), prev_log_len);
    for (uint64_t i = 1; i < leader->log_length(); ++i) {
        EXPECT_LE(leader->entry_term(i-1), leader->entry_term(i));
    }
}
```

### 16.3 Chaos Engineering (Docker Compose)

```yaml
# docker-compose.cluster.yml
services:
  jellybean-node-1:
    image: jellybean:dev
    command: ["./jellybean-server", "--node-id=1", "--peers=2:node2:9000,3:node3:9000"]
    networks: [jellybean-net]
    
  jellybean-node-2:
    image: jellybean:dev
    command: ["./jellybean-server", "--node-id=2", "--peers=1:node1:9000,3:node3:9000"]
    networks: [jellybean-net]
    
  jellybean-node-3:
    image: jellybean:dev
    command: ["./jellybean-server", "--node-id=3", "--peers=1:node1:9000,2:node2:9000"]
    networks: [jellybean-net]

  chaos:
    image: pumba/pumba
    volumes:
      - /var/run/docker.sock:/var/run/docker.sock
    command: >
      netem --duration 60s --delay 100ms --loss 5%
      jellybean-node-2  # Inject network chaos on node 2
    networks: [jellybean-net]

networks:
  jellybean-net:
    driver: bridge
```

---

## 17. Failure Recovery Design

```
Failure modes you MUST handle:

1. Process crash          → Raft log replay on restart
2. Network partition      → Leader detects no quorum, steps down
3. Slow follower          → Snapshot + log transfer (InstallSnapshot RPC)
4. Split brain            → Term number prevents stale leaders from committing
5. Disk full              → Reject writes, signal error, do NOT corrupt log
6. Memory exhaustion      → Arena OOM propagates as error, no terminate()
7. Deadlock               → Thread watchdog: detect blocked threads via heartbeat
8. Corrupt log entry      → CRC32C mismatch on segment read → truncate + re-replicate
```

```cpp
// Watchdog pattern — detect stuck threads
class ThreadWatchdog {
    std::atomic<uint64_t> last_heartbeat_{0};
    std::thread watchdog_thread_;
    
    void monitor() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            uint64_t hb = last_heartbeat_.load(std::memory_order_acquire);
            uint64_t now = now_monotonic_ms();
            if (now - hb > 10'000) {  // 10 second timeout
                // Dump stack traces of all threads
                dump_all_thread_stacks();
                // In production: alert, restart, not abort
                std::abort();  // during dev
            }
        }
    }
public:
    void heartbeat() {
        last_heartbeat_.store(now_monotonic_ms(), std::memory_order_release);
    }
};
```

---

## 18. Development Environment Setup

```dockerfile
# Dockerfile.dev
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    clang-17 clang-format-17 clang-tidy-17 \
    cmake ninja-build \
    linux-tools-generic \     # perf
    bpftrace \
    valgrind \
    heaptrack \
    liburing-dev \
    numactl libnuma-dev \
    google-perftools \        # tcmalloc + heap profiler
    libgtest-dev \
    libbenchmark-dev \
    strace ltrace gdb \
    && rm -rf /var/lib/apt/lists/*

# Set clang as default
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang-17 100
RUN update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-17 100

WORKDIR /jellybean
```

```bash
# WSL2 bootstrap (run once)
wsl --set-default-version 2
wsl --install Ubuntu-22.04

# Inside WSL2:
sudo apt-get install -y clang-17 cmake ninja-build liburing-dev bpftrace
sudo apt-get install -y linux-tools-$(uname -r)  # perf

# Clone and build
git clone https://github.com/yourname/jellybean.git
cd jellybean
cmake --preset release
ninja -C build/release
```

---

## 19. Implementation Milestones Checklist

```
Phase 1 — Foundation
  □ ArenaAllocator with block chain, reset(), stats
  □ SlabAllocator<T> with free list
  □ Huge page allocator with fallback
  □ SpscQueue<T, N> — benchmarked at >100M ops/sec
  □ MpmcQueue<T, N> — benchmarked at >50M ops/sec
  □ Seqlock
  □ Exponential backoff helper
  □ CPU topology detection (P-core vs E-core, NUMA node)
  □ BENCHMARK: allocator vs malloc vs tcmalloc vs jemalloc
  □ BENCHMARK: spsc vs boost::lockfree vs folly

Phase 2 — Reactor
  □ io_uring backend (read, write, accept, connect)
  □ epoll backend (fallback)
  □ Timer wheel (O(1) insert, expire)
  □ C++20 coroutine Task<T>
  □ per-core Reactor with run loop
  □ Thread pinning to P-cores
  □ BENCHMARK: io_uring vs epoll on loopback TCP

Phase 3 — Actor Runtime
  □ Message type with inline storage
  □ ActorRef (typed handle to actor)
  □ Shard-local actor registry
  □ Work-stealing deque (Chase-Lev)
  □ Fiber scheduler per shard
  □ Cross-shard message routing via SPSC
  □ BENCHMARK: actor message throughput, latency histogram

Phase 4 — Network + Protocol
  □ TCP connection with io_uring backend
  □ Buffer pool with huge pages
  □ Scatter-gather I/O
  □ Wire frame (header + CRC32C)
  □ Zero-copy parse
  □ SIMD CRC32C (SSE4.2)
  □ Connection pool + reconnect logic
  □ BENCHMARK: loopback ping-pong latency

Phase 5 — Log + Raft
  □ LogSegment (mmap write + read)
  □ Segment rolling + index
  □ Group commit (batched fsync)
  □ RaftNode (election, AppendEntries, commit)
  □ InstallSnapshot RPC
  □ Raft correctness tests (invariant checks)
  □ Docker 3-node cluster test
  □ BENCHMARK: log append throughput, raft commit latency

Phase 6 — Cluster Runtime
  □ Distributed actor registry (Raft-backed)
  □ Cross-node message routing
  □ Task execution API (submit → result)
  □ eBPF probes on hot paths
  □ Flamegraph CI (auto-generate on perf regression)
  □ Prometheus metrics endpoint
  □ Grafana dashboard
  □ Chaos test: leader kill + re-election
  □ BENCHMARK: end-to-end distributed task latency
```

---

## 20. What MAANG Interviewers Will Ask About This Project

When you present this project, be prepared for these deep dives:

**Memory**:
- "Walk me through your arena allocator. What happens when a block is full?"
- "Why cache-line alignment for your atomic head/tail?"
- "How does your huge page allocator fall back gracefully?"

**Concurrency**:
- "What memory ordering do you use in your SPSC queue and why?"
- "What is the ABA problem? Does your MPMC queue have it? Why not?"
- "How does your work-stealing deque handle concurrent push and steal?"

**I/O**:
- "What's the difference between io_uring SQPOLL and epoll?"
- "Explain zero-copy networking. What does MSG_ZEROCOPY actually do?"
- "How do you handle partial reads on your binary protocol?"

**Distributed Systems**:
- "What is Raft's leader completeness property and how do you test it?"
- "How do you handle a follower that's 1000 log entries behind?"
- "What is split-brain? How does Raft prevent it?"

**Performance**:
- "What is false sharing? Show me in your code where you prevent it."
- "Your flamegraph shows 40% time in memcpy. How do you fix it?"
- "What does `perf stat` tell you about your ring buffer implementation?"

---

## 21. Recommended Reading

**Papers** (read these in order):
1. "The Raft Consensus Algorithm" — Ongaro & Ousterhout (2014)
2. "Hashed and Hierarchical Timing Wheels" — Varghese & Lauck (1997)
3. "Dynamic Circular Work-Stealing Deque" — Chase & Lev (2005)
4. "Scalable Lock-Free Dynamic Memory Allocation" — Maged Michael (2004)
5. "io_uring documentation" — Jens Axboe (kernel.dk)
6. "DPDK documentation" (conceptual, not implementation)

**Books**:
- "C++ Concurrency in Action" — Anthony Williams (memory model bible)
- "Systems Performance" — Brendan Gregg (perf/eBPF/flamegraph)
- "Designing Data-Intensive Applications" — Kleppmann (distributed systems)
- "Is Parallel Programming Hard?" — Paul McKenney (RCU, memory barriers)

**Code to Read** (in this order of complexity):
1. Aeron C++ client (lock-free ring buffer, binary protocol)
2. Seastar source (per-core reactor, DPDK integration)
3. RocksDB memtable (arena allocator, skiplist)
4. Folly `ProducerConsumerQueue`, `MPMCQueue`
5. Linux kernel rbtree, list_head (intrusive data structures)
```