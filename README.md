# Jellybean Runtime (Test-Driven Systems Prototype)

Jellybean is a C++ systems runtime project focused on inference runtime internals:
- lock-free queues and scheduler primitives
- reactor/timer infrastructure
- memory allocators (arena/slab)
- inference runtime and LibTorch backend
- **Production-grade Inference Server (`jellybean_server`)**

## Dependency Model

`vcpkg` is **not required**.

Primary requirements:
- CMake 3.25+
- C++23 compiler
- Ninja (recommended)
- LibTorch (for inference backend tests)
- GTest

## Build And Run Tests

Linux/macOS:

```bash
cmake -S . -B build -DENABLE_TORCH=ON -DLIBTORCH_ROOT=/opt/libtorch
cmake --build build --config Release --target unit_tests
ctest --test-dir build --output-on-failure
```

Windows:

```powershell
cmake -S . -B build -DENABLE_TORCH=ON -DLIBTORCH_ROOT=C:/deps/libtorch
cmake --build build --config Release --target unit_tests
ctest --test-dir build --output-on-failure
```

## Demo-Style Test Runs

Use the pedagogical walkthrough tests as live demos:

Linux/macOS:

```bash
./build/tests/unit/unit_tests --gtest_filter="PedagogicalWalkthroughTest.*"
```

Windows:

```powershell
.\build\tests\unit\Release\unit_tests.exe --gtest_filter="PedagogicalWalkthroughTest.*"
```

Helper scripts:
- `scripts/run_server.sh` (now runs demo-style test filters)
- `scripts/run_server.ps1` (same behavior on Windows)

## Repo Layout

- `include/jellybean/` core headers
- `src/` runtime implementations
- `src/server/main.cpp` production server entry point
- `tests/unit/` unit + pedagogical demonstration tests
- `docs/` architecture, findings, and production-readiness notes
- `configs/` server configuration files
- `tools/` client and profiling utilities
