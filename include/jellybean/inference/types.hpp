#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace jellybean::inference {

enum class DeviceKind {
    Cpu,
    Cuda
};

struct InferenceRequest {
    std::string model_id;
    std::vector<int64_t> shape;
    std::vector<float> input;
    DeviceKind device{DeviceKind::Cpu};
};

struct InferenceResponse {
    bool ok{false};
    std::string error;
    std::vector<int64_t> shape;
    std::vector<float> output;
    uint64_t latency_ns{0};
};

} // namespace jellybean::inference
