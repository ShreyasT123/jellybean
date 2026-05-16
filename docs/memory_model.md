# Jellybean Memory Model (Current)

## Queue and Worker Semantics
- Each registered model owns a bounded ring-style queue.
- Producers enqueue requests under mutex with timeout backpressure.
- Workers poll model queues and execute backend inference.

## Safety Goals
- No unbounded queue growth.
- Deterministic enqueue rejection when full and timeout expires.
- Clean shutdown by signaling queue stop and joining workers.

## Current Tradeoffs
- Uses mutex/condition variable for correctness and simplicity.
- Runtime currently favors clarity over peak lock-free throughput.
- Next phase can replace queue internals while preserving API.
