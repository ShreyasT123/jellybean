# Memory & Concurrency Model Notes

## Primary Hot-Path Memory Decisions
- **Zero-Allocation Logging**: Swapped heavy heap-allocated stream classes (`std::ostringstream`) for high-speed dynamic `spdlog` backed by `std::format` (fully optimized for C++23 header-only targets).
- **MPMC Task Pipelines**: Dedicated lock-free ring buffers (`mpmc_queue`) decouple reactor TCP threads from model execution threads, eliminating lock contention.
- **Zero-Spin Coroutine Wakening**: The `FutureAwaitable` relies on specialized detached threads inside a global Reactor continuation pool to schedule suspended fibers, bypassing the CPU-expensive coroutine busy-poll loop.
- **Arena Allocations**: Request and session I/O buffers are recycled and allocated from slab/arena memory maps, preventing fragmentation.

## Safety & Concurrency Rules
- **Non-Owning Views**: `std::span` arguments passed to the dynamic batch worker threads are guaranteed valid because the calling fiber is suspended via `FutureAwaitable` and kept alive until the background task completes and wakes it.
- **Dynamic Routing Isolation**: Different models (`ModelExecutor` instances) operate in separate thread contexts and memory pools to prevent inter-model latency spillover.
- **Thread Pools**: Continuation thread pools handle async event resolution to avoid blocking the main Reactor epoll event loop.

## Current Risks To Track
- **Tail Latency Contention**: When multiple concurrent connections (e.g. concurrency=8) hit highly intensive TorchScript execution blocks on single-socket CPUs, CPU cache line bouncing and OS scheduler context switching can impact tail (P95/P99) latencies.
- **Queue Bounds**: Bounded queue backpressure guarantees server stability, but peak bursts above execution capacity will trigger quick client rejection to prevent OOM.
