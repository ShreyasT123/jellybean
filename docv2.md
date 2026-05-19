# Jellybean (v2): C++ Inference Runtime Prototype

> **Project Update**: The aim and intent of Jellybean have pivoted. Originally conceptualized as a broad distributed systems and actor-model toolkit, the project is now laser-focused on building a high-performance, Triton-style C++ Inference Runtime.

Jellybean is a portfolio-grade systems prototype designed to demonstrate low-level systems control, measurable performance behavior, correctness, and a clear path to distributed serving. It focuses on taking deep learning models (via LibTorch) and serving them reliably over a TCP-based binary protocol.

---

## 1. North-Star Vision

A user or reviewer should be able to run a single command, send live inference requests, and reliably inspect:
- The full model load and inference execution path.
- Bounded queue and backpressure behavior under load.
- Request validation and strict failure handling.
- System throughput alongside p50/p99 latency metrics.
- Reproducible, config-driven system state and structured logs.

---

## 2. Core Architecture

Jellybean's architecture keeps the data plane simple, measurable, and swappable:

1. **Client / Network Layer**: Clients send requests using a framed binary protocol over TCP.
2. **Validation**: The server strict-validates the request shape and payload size against a provided `server.config`.
3. **Runtime & Scheduling**: Valid requests (`InferenceRequest`) are placed into a bounded, per-model work queue. This prevents unbounded memory growth.
4. **Execution**: Worker threads dequeue tasks and execute them using the backend (currently LibTorch) for TorchScript (`model.pt`) inference.
5. **Response**: Outputs are shape/size validated and pushed back to the client over the framed protocol.

**Design Principles:**
- Enforce contracts at the network boundary.
- Avoid unbounded queues (employ enqueue timeouts/backpressure).
- Keep model backends abstracted and swappable.

---

## 3. Directory Structure

```text
jellybean/
├── include/jellybean/inference/   # Inference API and runtime header definitions
├── src/inference/                 # Backend and queue/worker runtime implementations
├── src/server/main.cpp            # Production TCP inference server entrypoint
├── tools/infer_client.py          # Python client for load testing and validation
├── scripts/                       # Cross-platform run scripts (run_server.ps1, run_server.sh)
├── docs/                          # Architecture, protocol, and roadmap documentation
└── server.config                  # Key=Value configuration for model IO and server settings
```

---

## 4. Configuration (`server.config`)

The system is entirely driven by configuration to ensure reproducible test runs. Key fields include:
- `model_id`, `model_path`: Identification and file path for the TorchScript model.
- `host`, `port`: TCP binding parameters.
- `input_shape`, `expected_output_elems`: Tensor validation contracts.
- `workers`, `queue_size`, `enqueue_timeout_ms`: Runtime scheduler and backpressure tunables.
- `max_requests`: Automatic termination trigger (`0` for continuous run).
- `log_file`: Destination for structured system events.

---

## 5. Development Roadmap (Phases)

### Phase 1: Runtime Maturity (In Progress)
- Implement request deadlines and timeout response semantics.
- Expose runtime counters (queue depth, enqueue rejections, request success/fail ratios).
- Implement latency histograms (p50/p95/p99).
- Enrich structured logging (request id, model id, client IP, latency).

### Phase 2: Dynamic Batching
- Group requests based on `(model_id, device, shape)` compatibility.
- Implement tunables: `max_batch_size` and `max_batch_delay_us`.
- Support multiple flush triggers: `full`, `timeout`, and `drain`.

### Phase 3: Transport Upgrade
- Migrate from a thread-per-connection model to event-driven I/O (accept/read/write).
- Harden protocol error handling and test against malformed frames.

### Phase 4: Observability & Profiling
- Export metrics via a Prometheus text endpoint or file dump.
- Capture run metadata (CPU model, threads, build flags) during benchmarks.

### Phase 5: GPU Acceleration Path
- Implement device-aware scheduling (`cpu`, `cuda:0`, etc.).
- Maintain isolated worker pools per physical device.
- Ensure API compatibility across CPU-only and GPU-enabled modes.

### Phase 6: Distributed Slice (Minimal Control Plane)
- Introduce leader election and model registry ownership.
- Demonstrate multi-node health checking and failover routing.

*(Note: Full Triton feature parity or massive Kubernetes scaling are explicitly non-goals for this cycle.)*

---

## 6. Quick Start

### Windows
```powershell
.\scripts\run_server.ps1 -LibTorchRoot C:/deps/libtorch
python .\tools\infer_client.py --host 127.0.0.1 --port 9000 --shape 1,128,512 --requests 40
```

### Linux (WSL2 / Ubuntu)
```bash
LIBTORCH_ROOT=/opt/libtorch ./scripts/run_server.sh
python3 tools/infer_client.py --host 127.0.0.1 --port 9000 --shape 1,128,512 --requests 40
```

Alternatively, use `make`:
```bash
make build
make server
make client
```