#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <span>

namespace jellybean::inference {

enum class DeviceKind {
    Cpu,
    Cuda
};

struct InferenceRequest {
    std::string model_id;
    std::vector<int64_t> shape;
    std::span<const float> input;
    std::span<float> output_buffer; // Pre-allocated by network thread Arena
    DeviceKind device{DeviceKind::Cpu};
};

struct InferenceResponse {
    bool ok{false};
    std::string error;
    std::vector<int64_t> shape;
    uint32_t output_elems_written{0};
    uint64_t latency_ns{0};
    
    // Performance instrumentation (Phase 3 regression fixing)
    uint64_t routing_ns{0};
    uint64_t queue_wait_ns{0};
    uint64_t execution_ns{0};
    uint64_t send_ns{0};
};

} // namespace jellybean::inference
