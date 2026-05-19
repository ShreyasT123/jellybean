#include "jellybean/telemetry/metrics.hpp"
#include "jellybean/inference/runtime.hpp"

namespace jellybean::telemetry {

uint64_t LatencyHistogram::percentile(double p) const noexcept {
    uint64_t total = 0;
    for (const auto& b : buckets_) {
        total += b.load(std::memory_order_relaxed);
    }
    if (total == 0) return 0;

    uint64_t target = static_cast<uint64_t>(static_cast<double>(total) * p);
    uint64_t count = 0;
    for (size_t i = 0; i < buckets_.size(); ++i) {
        count += buckets_[i].load(std::memory_order_relaxed);
        if (count >= target) {
            return (uint64_t{1} << (i > 0 ? i - 1 : 0));
        }
    }
    return 0;
}

} // namespace jellybean::telemetry

namespace jellybean::inference {

auto InferenceRuntime::get_all_metrics() const -> std::vector<jellybean::telemetry::ModelExecutorMetrics> {
    std::vector<jellybean::telemetry::ModelExecutorMetrics> res;
    // Note: We perform const_cast because we need to lock mu_, which is a std::mutex (non-const)
    std::lock_guard lock(const_cast<std::mutex&>(mu_));
    res.reserve(executors_.size());
    for (const auto& [id, executor] : executors_) {
        const auto& raw_m = executor->metrics();
        const auto& raw_h = executor->latency_hist();
        jellybean::telemetry::PlainMetrics pm{
            .requests_received = raw_m.requests_received.load(std::memory_order_relaxed),
            .requests_completed = raw_m.requests_completed.load(std::memory_order_relaxed),
            .requests_rejected = raw_m.requests_rejected.load(std::memory_order_relaxed),
            .queue_timeouts = raw_m.queue_timeouts.load(std::memory_order_relaxed),
        };
        res.push_back({
            .model_id = id,
            .metrics = pm,
            .p50_ns = const_cast<telemetry::LatencyHistogram&>(raw_h).p50(),
            .p95_ns = const_cast<telemetry::LatencyHistogram&>(raw_h).p95(),
            .p99_ns = const_cast<telemetry::LatencyHistogram&>(raw_h).p99(),
        });
    }
    return res;
}

} // namespace jellybean::inference
