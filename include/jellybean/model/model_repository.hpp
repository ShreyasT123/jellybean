#pragma once

#include <string>
#include <vector>

#include "jellybean/model/model_config.hpp"
#include "jellybean/model/model_registry.hpp"

namespace jellybean::model {

/**
 * @brief Scans a model repository directory and loads all discovered models
 *        into a ModelRegistry.
 *
 * Expected layout:
 *   <repo_root>/
 *     ├── bert/
 *     │    ├── config.ini
 *     │    └── 1/
 *     │         └── model.pt
 *     └── resnet/
 *          ├── config.ini
 *          └── 3/
 *               └── model.pt
 *
 * For each model directory:
 *   1. Parse config.ini via ModelConfig::from_dir()
 *   2. Create a backend based on config.backend
 *   3. Call backend->load()
 *   4. Register into the registry as READY (or FAILED on error)
 *
 * Errors in one model do NOT abort loading of others — failed models
 * are registered with state=FAILED and a warning is logged.
 */
class ModelRepository {
   public:
    struct LoadResult {
        int loaded{0};   // models successfully loaded to READY
        int failed{0};   // models that failed to load
        int skipped{0};  // directories that didn't look like models
        std::vector<std::string> failed_names;
    };

    /**
     * @brief Scan `repo_root` and populate `registry`.
     * @param repo_root  Filesystem path to the repository root directory.
     * @param registry   Registry to register discovered models into.
     * @return           Summary of what was loaded/failed/skipped.
     */
    [[nodiscard]] static auto scan_and_load(
        const std::string& repo_root,
        ModelRegistry&     registry) -> LoadResult;

    /**
     * @brief Load (or reload) a single model directory into the registry.
     * If the model is already registered, transitions it through UNLOADING→LOADING→READY.
     * @return true on success.
     */
    [[nodiscard]] static auto load_model(
        const std::string& model_dir,
        ModelRegistry&     registry) -> bool;
};

}  // namespace jellybean::model
