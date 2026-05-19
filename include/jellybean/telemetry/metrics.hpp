#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

namespace jellybean::telemetry {

// Lock-free runtime counters
struct RuntimeMetrics {
    alignas(64) std::atomic<uint64_t> requests_received{0};
    alignas(64) std::atomic<uint64_t> requests_completed{0};
    alignas(64) std::atomic<uint64_t> requests_rejected{0};
    alignas(64) std::atomic<uint64_t> queue_timeouts{0};
};

// HDR Histogram for capturing exact tail percentiles without float rounding
class LatencyHistogram {
    std::array<std::atomic<uint64_t>, 1024> buckets_{};

    static auto bucket_for(uint64_t latency_ns) noexcept -> size_t {
        if (latency_ns == 0) return 0;
        return std::min(static_cast<size_t>(64 - __builtin_clzll(latency_ns)), size_t{1023});
    }

   public:
    void record(uint64_t latency_ns) noexcept {
        buckets_[bucket_for(latency_ns)].fetch_add(1, std::memory_order_relaxed);
    }

    auto percentile(double p) const noexcept -> uint64_t;

    auto p50() const -> uint64_t {
        return percentile(0.50);
    }
    auto p95() const -> uint64_t {
        return percentile(0.95);
    }
    auto p99() const -> uint64_t {
        return percentile(0.99);
    }
    auto p999() const -> uint64_t {
        return percentile(0.999);
    }
};

}  // namespace jellybean::telemetry
