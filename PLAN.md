# PLAN: Production-Ready Jellybean (MAANG-Level)

## Objective
Transform Jellybean from a strong systems prototype into a production-grade inference runtime platform with clear reliability, security, and operability guarantees.

## Phase 0: Foundations (Completed / In Progress)
1. Enforce clean source boundaries (`src/` runtime code only).
2. Remove demo-binary drift; use tests as executable demos.
3. Keep build/scripts/docs consistent with real targets.

## Phase 1: Correctness & Safety Hardening
1. Add strict bounds checks and overflow-safe size math at every request boundary.
2. Add mandatory invariant checks for batch response cardinality.
3. Add ASan/UBSan/TSan CI lanes with fail-fast policy.
4. Introduce fuzzing targets for protocol/codec/parser paths.

Exit Criteria:
- All sanitizer lanes green.
- Fuzzers run in CI/nightly with crash triage workflow.

## Phase 2: Reliability Engineering
1. Define request lifecycle state machine and timeout semantics.
2. Add graceful shutdown/drain tests and queue pressure tests.
3. Add fault-injection tests: backend errors, queue saturation, slow consumers.
4. Add deterministic load/regression tests for p50/p95/p99 drift.

Exit Criteria:
- Documented SLO envelopes and proven behavior under controlled failures.

## Phase 3: Observability & Operations
1. Standardize structured logs (request_id, model_id, status, error_code, latency).
2. Export Prometheus metrics and OpenTelemetry traces.
3. Add health/readiness/liveness surfaces.
4. Add runbooks for top operational incidents.

Exit Criteria:
- On-call-ready dashboards and runbooks.

## Phase 4: Security Baseline
1. Add transport security (TLS) and service authn/authz.
2. Add input-size/rate limits and abuse controls.
3. Add SBOM generation and dependency/license scanning.
4. Add supply-chain protections for build/release artifacts.

Exit Criteria:
- Security review signoff with tracked threat model mitigations.

## Phase 5: Performance Engineering
1. CPU pinning and NUMA-aware worker placement.
2. Memory pool and batching policy tuning per workload profile.
3. Perf CI with golden baselines and regression gates.
4. Workload profile matrix (short/long sequence, burst/steady load).

Exit Criteria:
- Reproducible performance reports with enforced regression budgets.

## Phase 6: Platformization
1. Add stable external API contract and versioning strategy.
2. Add deployment manifests and environment configuration profiles.
3. Add canary + rollback release controls.
4. Add compatibility matrix and upgrade playbooks.

Exit Criteria:
- Safe upgrade path and release governance equivalent to mature infra teams.

## Hiring-Signal Deliverables (MAANG Bar)
1. Architecture Decision Records (ADRs) for core tradeoffs.
2. Incident postmortem templates + at least 2 game-day simulations.
3. Benchmarks with methodology doc and reproducibility script.
4. Security + reliability scorecards reviewed each release.
