# Jellybean Architecture (Current)

## High-Level Flow
1. Client sends framed request over TCP.
2. Server validates request shape and size against `server.config`.
3. Valid request is converted to `InferenceRequest`.
4. `InferenceRuntime` enqueues work into a bounded per-model queue.
5. Worker thread dequeues and executes backend infer call.
6. Server validates output shape and size and sends framed response.
7. Server logs startup/events/errors to stdout and file.

## Main Components
- `src/demo/infer_server_demo.cpp`
  - TCP accept loop
  - request parsing/validation
  - response framing
  - config loading and logging
- `include/jellybean/inference/runtime.hpp`
  - runtime config and queue/worker APIs
- `src/inference/runtime.cpp`
  - bounded queue plus worker scheduler
- `src/inference/torch_backend.cpp`
  - model load and inference path via LibTorch

## Design Intent
- Keep data plane simple and measurable.
- Enforce contracts at network boundary.
- Avoid unbounded queues and memory growth.
- Keep model backend swappable behind interface.
