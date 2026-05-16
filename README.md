# Jellybean Inference Runtime
Jellybean is a C++ inference runtime prototype in a Triton-style direction:
- LibTorch model loading and inference
- concurrency-safe runtime queues and worker scheduling
- TCP inference server with framed binary protocol
- Python client for load testing
- config-driven model I/O validation and file logging

This is a portfolio-grade systems prototype, not a production server.

## Current Demos
- `jellybean_infer_demo`: in-process runtime benchmark path
- `jellybean_infer_server_demo`: TCP server path using `server.config`
- `jellybean_torch_smoke`: LibTorch sanity check
- `jellybean_demo`: legacy systems-runtime checks (mailbox/timer/tcp echo)

## Quick Start (Windows)
```powershell
.\scripts\run_server.ps1 -LibTorchRoot C:/deps/libtorch
python .\tools\infer_client.py --host 127.0.0.1 --port 9000 --shape 1,128,512 --requests 40
```

## Quick Start (Linux)
```bash
LIBTORCH_ROOT=/opt/libtorch ./scripts/run_server.sh
python3 tools/infer_client.py --host 127.0.0.1 --port 9000 --shape 1,128,512 --requests 40
```

Or with make:
```bash
make build
make server
make client
```

## Config
Server reads `server.config` (key=value). Important fields:
- `model_id`, `model_path`
- `host`, `port`
- `input_shape`
- `expected_output_elems`
- `workers`, `queue_size`, `enqueue_timeout_ms`
- `max_requests` (`0` for no auto-stop)
- `log_file`

## Repo Layout
- `include/jellybean/inference/` -> inference API/runtime headers
- `src/inference/` -> backend and runtime implementation
- `src/demo/infer_server_demo.cpp` -> TCP inference server demo
- `tools/infer_client.py` -> Python load client
- `scripts/run_server.ps1` / `scripts/run_server.sh` -> build + run entrypoints
- `docs/` -> architecture, memory model, protocol, and roadmap docs

## Status
Implemented:
- model load/infer path with TorchScript (`model.pt`)
- bounded queue + worker runtime with enqueue timeout backpressure
- TCP server/client flow with validation and file logging

Next:
- dynamic batching
- deeper observability/metrics
- async transport/reactor integration
- multi-node control-plane slice
