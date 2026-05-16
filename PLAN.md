# Jellybean Plan (Inference + Systems Depth)

## Objective
Build a credible custom inference runtime that demonstrates:
- low-level systems control
- measurable performance behavior
- correctness and resilience
- clear path to distributed serving

## North-Star Demo
A reviewer can run one command, send live requests, and inspect:
- model load and inference path
- bounded queue/backpressure behavior
- request validation and failure handling
- throughput + p50/p99 latency
- reproducible config and logs

## Completed
1. Inference API and backend scaffolding (`types`, `backend`, `registry`).
2. LibTorch backend path (`load`, `infer`, CPU-first).
3. Runtime scheduler v1 (bounded per-model queue, worker threads, enqueue timeout).
4. TCP server demo + Python client.
5. Config-driven server startup and input/output contract validation.
6. Cross-platform run scripts (`run_server.ps1`, `run_server.sh`, `Makefile`).

## Phase 1: Runtime Maturity (next)
1. Add request deadline field and timeout response semantics.
2. Add runtime counters:
   - queue depth, enqueue rejects, request success/fail
   - latency histograms (p50/p95/p99)
3. Add structured log fields:
   - request id, model id, client ip, status, latency
4. Exit criteria:
   - stable behavior under sustained load with explicit rejection stats

## Phase 2: Dynamic Batching
1. Batch by `(model_id, device, shape)` compatibility.
2. Add `max_batch_size` and `max_batch_delay_us`.
3. Support flush reasons (`full`, `timeout`, `drain`).
4. Exit criteria:
   - throughput improvement vs non-batched baseline with reported tail latency impact

## Phase 3: Transport Upgrade
1. Move from thread-per-connection toward event-driven accept/read/write.
2. Harden protocol error handling and malformed frame tests.
3. Add separate long-running server and benchmark client executables.
4. Exit criteria:
   - predictable p99 under concurrent clients and connection churn

## Phase 4: Observability + Profiling
1. Add metrics export (Prometheus text endpoint or periodic metrics dump).
2. Add benchmark scripts with run metadata capture:
   - CPU model, threads, build flags, config hash
3. Track regression table in repo.
4. Exit criteria:
   - reproducible performance report committed

## Phase 5: GPU Path
1. Device-aware scheduling (`cpu`, `cuda:0`, ...).
2. Separate worker pools per device.
3. Fallback behavior when CUDA unavailable.
4. Exit criteria:
   - same API works in CPU-only and GPU-enabled modes

## Phase 6: Distributed Slice (minimal but real)
1. Introduce a tiny control plane:
   - leader election
   - model registry ownership
2. Node health and failover routing demo.
3. Exit criteria:
   - 2-3 node failover demo with continuing inference service

## Non-Goals (current cycle)
1. Full Triton feature parity.
2. Full Kubernetes control plane integration.
3. Multi-node model sharding at production scale.
