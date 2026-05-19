# Architecture

Jellybean is a highly modular, reactor-based asynchronous multi-model inference serving runtime library with high-performance TCP wire protocols.

## Core Modules

### 1. Multi-Model & Registry System (`src/model/*`)
- `model_config.hpp / .cpp`: Parses per-model Triton-style configurations (`config.ini`) resolving dynamic batching constraints, tensor dimensions, shapes, and backend mappings.
- `model_metadata.hpp`: Manages the thread-safe lifecycle states of loaded models (`UNLOADED`, `LOADING`, `READY`, `FAILED`).
- `model_registry.hpp / .cpp`: Registry acts as the single source of truth for active backends, managing thread-safe runtime lookups.
- `model_repository.hpp / .cpp`: Scans a structured repository directory (e.g. `models/`), resolving versions and dynamically loading model artifacts.

### 2. Dedicated Execution Engine (`src/model/model_executor.cpp`)
- Owns and manages a dedicated lock-free Multi-Producer Multi-Consumer (`mpmc_queue`) pipeline per model.
- Spins up targeted background CPU/GPU execution worker threads to process model inference tasks in dynamically batched workloads.

### 3. High-Performance Reactor & Fiber Scheduler (`src/reactor/*` & `src/scheduler/*`)
- Implements an epoll-based reactor event loop to manage hundreds of concurrent TCP client sessions multiplexed onto C++20 coroutines.
- Integrates a zero-spin event-driven `FutureAwaitable` that suspends client sessions and triggers Reactor wakeup via continuation pools instead of cpu-intensive busy-polling.

### 4. Telemetry & Zero-Allocation Logging (`src/telemetry/*`, `logging.hpp`)
- Replaces standard streams with zero-allocation dynamic `spdlog` macros.
- Serves server-side microsecond performance and execution telemetry on a dedicated observation port (9001).

## Model Repository Layout
Models are served out of a Triton-style structured folder layout:
```text
models/
└── <model_id>/
    ├── config.ini
    └── 1/
        └── model.pt
```
