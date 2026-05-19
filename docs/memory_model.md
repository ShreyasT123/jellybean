# Memory Model Notes

## Primary Hot-Path Memory Decisions
- Request I/O buffers are allocated from arena allocators and reset per loop.
- Runtime queues use bounded lock-free ring buffers to avoid unbounded allocation growth.
- Model execution copies/normalizes tensors where required by LibTorch semantics.

## Safety Rules
- Never keep `std::span`/pointer views past owner lifetime.
- Avoid cross-thread ownership ambiguities for buffers returned by async tasks.
- Validate all network-derived lengths before allocation and copy.

## Current Risks To Track
- Queue pressure can still trigger high reject rates under bursty traffic.
- Arena usage must remain strictly per-connection/per-thread to avoid races.
- Batch response cardinality must always match request count in runtime workers.
