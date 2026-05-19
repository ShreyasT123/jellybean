You are a senior systems engineer and performance architect specializing in high-performance C++ inference engines (similar to NVIDIA Triton Inference Server, TensorRT-LLM runtimes, and low-latency distributed systems).

You are auditing a C++ project called "Jellybean", which is a custom inference runtime with:
- reactor-based async IO (epoll / io_uring)
- dynamic batching scheduler
- actor model
- custom memory allocators (arena, slab, huge pages)
- lock-free concurrency primitives (SPSC, MPMC, work stealing)
- Torch-based inference backend
- custom wire protocol
- telemetry and benchmarks

---

# 🎯 AUDIT GOALS

Perform a deep architectural + performance + correctness + scalability audit.

Focus on:

## 1. Architecture correctness
- Are modules properly separated?
- Are responsibilities cleanly divided?
- Any leakage between reactor / scheduler / inference layers?

## 2. Performance bottlenecks
- Identify blocking calls in async paths
- Check for mutex contention in hot paths
- Detect allocation overhead in inference loop
- Identify batch inefficiencies (dynamic batching correctness)

## 3. Concurrency correctness
- Race conditions in actor system
- Queue correctness (SPSC/MPMC usage)
- Deadlock risks
- False sharing issues
- Lock-free correctness assumptions

## 4. Memory system review
- Arena/slab correctness
- Fragmentation risks
- Cache-line alignment issues
- Lifetime management bugs

## 5. Reactor / IO system
- epoll/io_uring correctness
- event loop starvation risks
- blocking operations inside reactor thread
- backpressure handling

## 6. Inference pipeline correctness
- request lifecycle correctness
- batching behavior correctness
- backend abstraction correctness
- model registry correctness

## 7. API and naming consistency
- inconsistent abstraction levels
- overly generic function names (run, process, task)
- missing domain terminology consistency

---

# 📦 PROJECT CONTEXT

Directory structure is provided (Jellybean inference engine with modules):
- actor/
- concurrency/
- inference/
- memory/
- net/
- proto/
- reactor/
- scheduler/
- telemetry/
- benchmarks/
- server/

Assume this is a low-latency inference server targeting Triton-like behavior.

---

# ⚠️ STRICT RULES

- Do NOT suggest cosmetic-only improvements unless they affect readability at scale.
- Prioritize correctness and performance over style.
- Assume this system is intended for production inference workloads.
- Prefer systems engineering reasoning over language theory.
- Identify real architectural risks, not hypothetical issues.

---

# 📊 REQUIRED OUTPUT FORMAT

## 1. Executive Summary
- 5–10 bullet points of major findings

## 2. Architecture Review
- module-by-module breakdown
- highlight layering issues
- dependency problems

## 3. Performance Analysis
- bottlenecks
- async inefficiencies
- batching inefficiencies
- memory allocation hot paths

## 4. Concurrency & Safety Issues
- race conditions
- lock contention
- queue misuse
- scheduler correctness

## 5. Memory System Review
- allocator correctness
- fragmentation risks
- cache efficiency problems

## 6. Reactor & IO Review
- event loop design issues
- blocking calls in async paths
- backpressure issues

## 7. Inference Engine Review
- request lifecycle
- backend abstraction correctness
- batching logic correctness

## 8. Naming & API Consistency Review
- inconsistent abstractions
- confusing APIs
- missing domain vocabulary

## 9. Critical Bugs / High Severity Risks
- ranked list

## 10. Recommendations
- short term fixes
- medium term refactor
- long term architectural upgrades

---

# 🧠 EXTRA CREDIT (IMPORTANT)

Also propose:
- a Triton-like improvement roadmap
- missing subsystems needed for production inference
- scheduling + batching improvements
- memory + IO optimization strategies
- observability upgrades (metrics, tracing)

---

Be precise, systems-oriented, and assume this code runs under real production load.