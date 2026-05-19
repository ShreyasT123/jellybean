````md
# Jellybean Phase 3 — Multi-Model Inference Runtime Architecture

Jellybean is evolving from a single-model async inference server into a modular high-performance inference runtime inspired by systems like Triton, TorchServe, vLLM, Ray Serve, and modern reactor-based async runtimes.

The previous phase implemented:
- coroutine-based async TCP runtime
- reactor/event-loop scheduling
- FutureAwaitable zero-spin wakeup path
- dynamic batching
- TorchScript inference execution
- bounded queues and backpressure
- latency/throughput benchmarking
- async vs sync transport benchmarking

The runtime now needs to evolve into a true multi-model serving platform.

---

# Primary Goals

Design and implement the next-generation Jellybean runtime architecture with:

- multi-model serving
- model repository management
- backend abstraction layer
- hot model reload/unload
- per-model batching and scheduling
- transport/runtime decoupling
- advanced observability
- scalable async execution
- production-style architecture

The system should resemble a lightweight Triton-style inference runtime implemented in modern C++20.

---

# Core Architectural Requirements

## 1. Model Repository System

Replace the current global:

```ini
model_path=model.pt
````

with a Triton-style model repository layout:

```text
models/
 ├── bert/
 │    ├── config.pbtxt
 │    ├── 1/model.pt
 │
 ├── llama/
 │    ├── config.pbtxt
 │    ├── 1/model.onnx
 │
 └── resnet/
      ├── config.pbtxt
      ├── 3/model.engine
```

The runtime should:

* scan model repositories at startup
* dynamically discover models
* support model versioning
* validate model configs
* register models into a runtime registry

Implement:

* ModelRegistry
* ModelMetadata
* ModelLoader
* VersionResolver

---

# 2. Backend Abstraction Layer

Current implementation is tightly coupled to TorchScript.

Refactor into backend interfaces.

Example:

```cpp
class IBackend {
public:
    virtual ~IBackend() = default;

    virtual InferenceResponse infer(
        const InferenceRequest& req
    ) = 0;
};
```

Implement initial backends:

* TorchScriptBackend
* MockBackend

Design architecture to later support:

* ONNX Runtime
* TensorRT
* GGML / llama.cpp
* OpenVINO
* custom CUDA kernels

Backends must be isolated from:

* networking
* batching
* routing
* transport layer

---

# 3. Per-Model Execution Contexts

Each model should own independent runtime state:

```text
ModelContext
 ├── scheduler
 ├── batching queue
 ├── metrics
 ├── backend executor
 ├── worker pool
 ├── memory arena
 └── config
```

This enables:

* heterogeneous scheduling
* independent batching policies
* isolated backpressure
* per-model scaling

---

# 4. Request Routing Layer

Current server handles one global inference endpoint.

Add:

* model-aware request routing
* request dispatch by model_id
* binary protocol model selection support

Example request flow:

```text
TCP Request
   ↓
Transport Layer
   ↓
Request Router
   ↓
Model Registry Lookup
   ↓
Model Scheduler
   ↓
Batch Executor
   ↓
Backend
```

Add:

* unknown model handling
* model health validation
* routing metrics

---

# 5. Dynamic Batching Evolution

Current batching is global.

Refactor into per-model batching systems.

Each model config should support:

```ini
max_batch_size=16
max_batch_delay_us=2000
preferred_batch_sizes=1,2,4,8,16
```

Scheduler goals:

* maximize throughput
* minimize tail latency
* avoid starvation
* support heterogeneous workloads

Investigate:

* EDF scheduling
* queue fairness
* adaptive batching
* microbatch fusion

---

# 6. Async Runtime Improvements

The previous phase introduced:

* FutureAwaitable
* zero-spin wakeups
* Reactor::schedule()

Continue evolving the runtime toward:

* lock-minimized scheduling
* intrusive task queues
* continuation stealing
* batched wakeups
* io_uring readiness integration
* NUMA-aware worker assignment
* coroutine frame pooling

Avoid:

* detached thread wakeups
* busy-spin polling
* excessive heap allocation

---

# 7. Memory Architecture

Current allocations likely create contention and fragmentation.

Implement:

* slab allocators
* arena allocators
* pooled coroutine frames
* reusable tensor buffers
* request object pooling

Goals:

* reduce heap churn
* improve cache locality
* stabilize tail latency

---

# 8. Observability & Metrics

Add production-style metrics.

Per-model:

* throughput
* queue depth
* batch efficiency
* average latency
* p50/p95/p99 latency
* dropped requests
* scheduler occupancy
* active workers

Runtime-wide:

* reactor utilization
* memory usage
* task queue depth
* wakeup counts
* coroutine suspension/resume counts

Add:

* structured logging
* metrics registry
* Prometheus-style export layer later

---

# 9. Configuration Refactor

Current config is global and static.

Move toward:

```ini
model_repository=./models
hot_reload=true
default_backend=torchscript
control_mode=explicit
metrics_enabled=true
```

Each model gets independent config files.

Config system should support:

* validation
* defaults
* live reload
* typed parsing

---

# 10. Hot Reload & Model Lifecycle

Add runtime model lifecycle management.

Support:

* load model
* unload model
* reload model
* version swap
* graceful drain

Without restarting the runtime.

Model lifecycle states:

* LOADING
* READY
* UNAVAILABLE
* FAILED
* UNLOADING

---

# 11. Benchmarking Evolution

Current benchmarks are simplistic localhost synthetic tensor tests.

Expand benchmarking into:

* multi-model workloads
* mixed request sizes
* concurrent socket floods
* async pipeline saturation
* queue contention
* batch efficiency analysis
* overload testing
* fairness testing
* cold model load latency
* hot reload latency

Add:

* Node.js async benchmark client
* high-concurrency socket stress tests
* pipelined requests
* C++ native benchmark client later

---

# 12. Networking Roadmap

Current transport:

* custom binary TCP protocol

Future transport support:

* HTTP/1.1
* HTTP/2
* WebSocket
* gRPC
* Unix sockets

Transport layer must remain decoupled from:

* backend execution
* batching
* scheduler logic

---

# 13. Reliability Goals

Implement:

* graceful shutdown
* backpressure propagation
* overload protection
* bounded queues
* timeout enforcement
* request cancellation
* worker isolation
* crash-safe model loading

---

# 14. Desired Engineering Style

Codebase expectations:

* modern C++20
* RAII-first design
* lock minimization
* cache-aware structures
* coroutine-centric async flow
* modular interfaces
* production-grade logging
* explicit ownership semantics
* low-allocation hot paths

Avoid:

* giant singleton classes
* tightly coupled networking/backend code
* blocking async workers
* hidden global state
* thread-per-request architectures

---

# Desired Final Runtime Architecture

```text
                +-------------------+
                | Transport Layer   |
                | TCP / HTTP / RPC  |
                +---------+---------+
                          |
                          v
                +-------------------+
                | Request Router    |
                +---------+---------+
                          |
                          v
                +-------------------+
                | Model Registry    |
                +---------+---------+
                          |
         +----------------+----------------+
         |                                 |
         v                                 v
+-------------------+          +-------------------+
| Model Scheduler A |          | Model Scheduler B |
+---------+---------+          +---------+---------+
          |                               |
          v                               v
+-------------------+          +-------------------+
| Batch Executor A  |          | Batch Executor B  |
+---------+---------+          +---------+---------+
          |                               |
          v                               v
+-------------------+          +-------------------+
| Torch Backend     |          | ONNX Backend      |
+-------------------+          +-------------------+
```

---

# Current Benchmark Results Motivation

Previous async runtime evolution achieved:

* throughput improvement: +148%
* average latency reduction: -40%
* p50 latency reduction: -48%

after replacing spin/yield polling with FutureAwaitable reactor wakeups.

The next phase should continue improving:

* scalability
* scheduling efficiency
* batching quality
* memory locality
* multi-model orchestration
* production runtime design

The goal is to evolve Jellybean into a serious experimental inference runtime and systems engineering project.
