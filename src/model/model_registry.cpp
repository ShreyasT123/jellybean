#include "jellybean/model/model_registry.hpp"

#include <algorithm>

namespace jellybean::model {

auto ModelRegistry::register_model(std::unique_ptr<ModelMetadata> meta) -> bool {
    if (!meta) return false;
    const std::string id = meta->config.model_id();
    std::lock_guard lock(mu_);
    auto [_, inserted] = models_.emplace(id, std::move(meta));
    return inserted;
}

auto ModelRegistry::lookup(const std::string& model_id) const -> ModelMetadata* {
    std::lock_guard lock(mu_);
    auto it = models_.find(model_id);
    if (it == models_.end()) return nullptr;
    return it->second.get();
}

auto ModelRegistry::lookup_ready(const std::string& model_id) const -> ModelMetadata* {
    ModelMetadata* m = lookup(model_id);
    if (!m || !m->is_ready()) return nullptr;
    return m;
}

auto ModelRegistry::unregister(const std::string& model_id) -> std::unique_ptr<ModelMetadata> {
    std::lock_guard lock(mu_);
    auto it = models_.find(model_id);
    if (it == models_.end()) return nullptr;
    auto node = models_.extract(it);
    return std::move(node.mapped());
}

auto ModelRegistry::list() const -> std::vector<ModelInfo> {
    std::lock_guard lock(mu_);
    std::vector<ModelInfo> out;
    out.reserve(models_.size());
    for (const auto& [id, meta] : models_) {
        out.push_back({
            .name    = id,
            .state   = meta->state.load(std::memory_order_acquire),
            .version = meta->config.version,
        });
    }
    return out;
}

auto ModelRegistry::size() const -> std::size_t {
    std::lock_guard lock(mu_);
    return models_.size();
}

}  // namespace jellybean::model
