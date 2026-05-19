#include "jellybean/telemetry/metrics.hpp"

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
