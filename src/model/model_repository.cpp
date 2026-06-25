#include "jellybean/model/model_repository.hpp"

#include <filesystem>
#include <memory>

#include "jellybean/core/errors.hpp"
#include "jellybean/core/logging.hpp"
#include "jellybean/inference/torch_backend.hpp"
#include "jellybean/model/model_config.hpp"
#include "jellybean/model/model_metadata.hpp"

namespace fs = std::filesystem;

namespace jellybean::model {

namespace {

auto make_backend(BackendKind kind) -> std::shared_ptr<inference::IInferenceBackend> {
    switch (kind) {
        case BackendKind::TorchScript:
#ifdef ENABLE_TORCH
            return inference::make_torch_backend();
#else
            JELLY_LOG_ERROR("[REPO] Torch backend requested but ENABLE_TORCH=OFF");
            return nullptr;
#endif
        case BackendKind::Onnx:
            JELLY_LOG_ERROR("[REPO] ONNX backend not yet implemented");
            return nullptr;
    }
    return nullptr;
}

}  // namespace

// ─── load_model ─────────────────────────────────────────────────────────────

auto ModelRepository::load_model(const std::string& model_dir,
                                 ModelRegistry&     registry) -> bool {
    ModelConfig cfg = ModelConfig::from_dir(model_dir);

    if (cfg.name.empty()) {
        JELLY_LOG_ERROR("[REPO] Failed to parse config in: {}", model_dir);
        return false;
    }

    JELLY_LOG_INFO("[REPO] Loading model '{}' v{} from {}", cfg.name, cfg.version, cfg.model_file_path);

    // Build metadata — start in LOADING state
    auto meta        = std::make_unique<ModelMetadata>(cfg);
    meta->state.store(ModelState::Loading, std::memory_order_release);

    auto* meta_ptr = meta.get();  // hold raw ptr before moving into registry

    // If model already registered, unregister old entry first (hot reload path)
    if (registry.lookup(cfg.name) != nullptr) {
        (void)registry.unregister(cfg.name);
    }

    if (!registry.register_model(std::move(meta))) {
        JELLY_LOG_ERROR("[REPO] Failed to register model '{}' (duplicate?)", cfg.name);
        return false;
    }

    auto backend = make_backend(cfg.backend);
    if (!backend) {
        meta_ptr->state.store(ModelState::Failed, std::memory_order_release);
        JELLY_LOG_ERROR("[REPO] Backend creation failed for '{}'", cfg.name);
        return false;
    }

    bool loaded = backend->load(cfg.name, cfg.model_file_path, cfg.device);
    if (!loaded) {
        meta_ptr->state.store(ModelState::Failed, std::memory_order_release);
        JELLY_LOG_ERROR("[REPO] Backend load failed for '{}'", cfg.name);
        return false;
    }

    meta_ptr->backend = std::move(backend);
    meta_ptr->state.store(ModelState::Ready, std::memory_order_release);

    JELLY_LOG_INFO("[REPO] Model '{}' v{} is READY", cfg.name, cfg.version);
    return true;
}

// ─── scan_and_load ───────────────────────────────────────────────────────────

auto ModelRepository::scan_and_load(const std::string& repo_root,
                                    ModelRegistry&     registry) -> LoadResult {
    LoadResult result;

    if (!fs::is_directory(repo_root)) {
        JELLY_LOG_ERROR("[REPO] Repository root does not exist: {}", repo_root);
        return result;
    }

    JELLY_LOG_INFO("[REPO] Scanning model repository: {}", repo_root);

    for (const auto& entry : fs::directory_iterator(repo_root)) {
        if (!entry.is_directory()) continue;

        const std::string model_dir = entry.path().string();
        const std::string model_name = entry.path().filename().string();

        // Skip hidden directories
        if (!model_name.empty() && model_name[0] == '.') {
            ++result.skipped;
            continue;
        }

        // A model directory must contain config.ini OR at least one numeric version subdir
        bool has_config  = fs::exists(entry.path() / "config.ini");
        bool has_version = false;
        if (!has_config) {
            for (const auto& sub : fs::directory_iterator(entry.path())) {
                if (!sub.is_directory()) continue;
                try { std::stoi(sub.path().filename().string()); has_version = true; break; }
                catch (...) {}
            }
        }

        if (!has_config && !has_version) {
            JELLY_LOG_DEBUG("[REPO] Skipping non-model directory: {}", model_name);
            ++result.skipped;
            continue;
        }

        bool ok = load_model(model_dir, registry);
        if (ok) {
            ++result.loaded;
        } else {
            ++result.failed;
            result.failed_names.push_back(model_name);
        }
    }

    JELLY_LOG_INFO("[REPO] Scan complete: {} loaded, {} failed, {} skipped",
                   result.loaded, result.failed, result.skipped);
    return result;
}

}  // namespace jellybean::model
