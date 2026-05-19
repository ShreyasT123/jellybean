#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "jellybean/inference/backend.hpp"
#include "jellybean/model/model_config.hpp"

namespace jellybean::model {

enum class ModelState {
    Unloaded,
    Loading,
    Ready,
    Failed,
    Unloading,
};

[[nodiscard]] inline auto model_state_str(ModelState s) -> const char* {
    switch (s) {
        case ModelState::Unloaded:  return "UNLOADED";
        case ModelState::Loading:   return "LOADING";
        case ModelState::Ready:     return "READY";
        case ModelState::Failed:    return "FAILED";
        case ModelState::Unloading: return "UNLOADING";
    }
    return "UNKNOWN";
}

/**
 * @brief Runtime metadata for a loaded model.
 *
 * Holds the parsed config, lifecycle state, and the live backend handle.
 * State transitions are atomic so the registry can be inspected from
 * any thread without holding a lock.
 */
struct ModelMetadata {
    ModelConfig                                     config;
    std::atomic<ModelState>                         state{ModelState::Unloaded};
    std::shared_ptr<inference::IInferenceBackend>   backend;

    // Non-copyable (atomic + shared_ptr ownership)
    ModelMetadata() = default;
    explicit ModelMetadata(ModelConfig cfg) : config(std::move(cfg)) {}
    ModelMetadata(const ModelMetadata&)            = delete;
    ModelMetadata& operator=(const ModelMetadata&) = delete;
    ModelMetadata(ModelMetadata&&)                 = delete;
    ModelMetadata& operator=(ModelMetadata&&)      = delete;

    [[nodiscard]] auto is_ready() const noexcept -> bool {
        return state.load(std::memory_order_acquire) == ModelState::Ready;
    }
};

}  // namespace jellybean::model
