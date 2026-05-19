#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jellybean/inference/types.hpp"

namespace jellybean::model {

enum class BackendKind {
    TorchScript,
    Onnx,     // future
};

/**
 * @brief Parsed configuration for a single model loaded from config.ini.
 *
 * Each model directory contains exactly one config.ini:
 *   models/bert/config.ini
 *
 * Example config.ini:
 *   name=bert
 *   backend=torchscript
 *   version=1
 *   device=cpu
 *   max_batch_size=8
 *   max_batch_delay_us=2000
 *   input_shape=1,128,512
 */
struct ModelConfig {
    std::string              name;
    BackendKind              backend{BackendKind::TorchScript};
    int                      version{1};
    inference::DeviceKind    device{inference::DeviceKind::Cpu};
    std::size_t              max_batch_size{4};
    int64_t                  max_batch_delay_us{1000};
    std::vector<int64_t>     input_shape;

    // Derived at load time — absolute path to the versioned model file
    // e.g.  models/bert/1/model.pt
    std::string              model_file_path;

    // Unique serving key: same as `name`
    [[nodiscard]] auto model_id() const -> const std::string& { return name; }

    /**
     * @brief Parse a config.ini in `model_dir`.
     * Fills `model_file_path` by scanning `model_dir/<version>/` for model.*
     * Returns a default-constructed ModelConfig (with name="") on failure.
     */
    [[nodiscard]] static auto from_dir(const std::string& model_dir) -> ModelConfig;
};

}  // namespace jellybean::model
