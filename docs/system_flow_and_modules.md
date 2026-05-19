# System Flow And Modules

## Development Flow
1. Build `unit_tests`.
2. Run targeted test suites.
3. Use pedagogical tests as executable demos.

## Test-as-Demo Surfaces
- Memory behavior demos (arena/slab)
- Lock-free queue behavior demos
- Reactor/timer behavior demos
- Scheduler and work-stealing demos
- Proto/codec demonstrations
- Actor model demos
- Inference runtime demos

## Operational Principle
If a subsystem cannot be demonstrated through a deterministic test, it is not considered ready.
