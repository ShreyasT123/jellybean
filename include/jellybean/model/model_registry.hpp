#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "jellybean/core/errors.hpp"
#include "jellybean/model/model_metadata.hpp"

namespace jellybean::model {

/**
 * @brief Thread-safe registry of all models known to the runtime.
 *
 * The registry is the single source of truth for:
 *   - which models exist
 *   - their lifecycle state (LOADING / READY / FAILED / UNLOADING)
 *   - their backend handles
 *
 * All public methods are safe to call from any thread.
 * Hot path `lookup()` acquires a shared_lock so concurrent readers don't contend.
 */
class ModelRegistry {
   public:
    ModelRegistry()  = default;
    ~ModelRegistry() = default;

    ModelRegistry(const ModelRegistry&)            = delete;
    ModelRegistry& operator=(const ModelRegistry&) = delete;

    /**
     * @brief Register a new model slot. Fails if the model_id already exists.
     * @return false if already registered.
     */
    [[nodiscard]] auto register_model(std::unique_ptr<ModelMetadata> meta) -> bool;

    /**
     * @brief Look up a model by id. Returns nullptr if not found.
     * Does NOT require the model to be READY — caller checks state.
     */
    [[nodiscard]] auto lookup(const std::string& model_id) const -> ModelMetadata*;

    /**
     * @brief Look up a model that is READY to serve. Returns nullptr if not found or not ready.
     */
    [[nodiscard]] auto lookup_ready(const std::string& model_id) const -> ModelMetadata*;

    /**
     * @brief Unregister and return ownership of a model's metadata.
     * Caller is responsible for draining in-flight requests before calling this.
     * Returns nullptr if not found.
     */
    [[nodiscard]] auto unregister(const std::string& model_id) -> std::unique_ptr<ModelMetadata>;

    /**
     * @brief Snapshot of all registered model IDs and their current states.
     */
    struct ModelInfo {
        std::string name;
        ModelState  state;
        int         version;
    };
    [[nodiscard]] auto list() const -> std::vector<ModelInfo>;

    /**
     * @brief Number of models currently registered (any state).
     */
    [[nodiscard]] auto size() const -> std::size_t;

   private:
    mutable std::mutex                                          mu_;
    std::unordered_map<std::string, std::unique_ptr<ModelMetadata>> models_;
};

}  // namespace jellybean::model
