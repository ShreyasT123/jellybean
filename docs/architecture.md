# Architecture

Jellybean is now structured as a runtime library plus test-driven demos.

## Core Modules
- `src/inference/runtime.cpp`: request scheduling, queueing, batching, metrics.
- `src/inference/torch_backend.cpp`: LibTorch-backed inference.
- `src/reactor/*`: event loop and backend abstractions.
- `src/scheduler/*`: scheduler internals.
- `src/proto/*`: wire/codec primitives.
- `src/memory/*`: arena and huge page utilities.
- `src/actor/*`: actor/mailbox runtime primitives.

## Demonstration Model
There are no `src/demo` executables. Demonstrations are executed via test targets, especially:
- `PedagogicalWalkthroughTest.*`

## Why This Shape
- Keeps production-code candidates in `src/` clean and focused.
- Prevents demo-only binaries from drifting out of sync.
- Forces runnable documentation through reproducible tests.
