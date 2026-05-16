# Distributed Roadmap (Raft and Control Plane)

Current implementation is single-node serving. Raft is not integrated into the active inference server path yet.

## Intended Minimal Distributed Slice
1. Leader election for control-plane ownership.
2. Replicated model registry metadata.
3. Node health heartbeat and failover of request routing.

## Why This Matters
- Demonstrates distributed correctness beyond local throughput.
- Aligns project with original systems intent from `DOC.md`.
- Provides stronger signal for advanced infrastructure interviews.

## Next Practical Step
- Introduce a small control service process with term/leader state and model ownership records.
