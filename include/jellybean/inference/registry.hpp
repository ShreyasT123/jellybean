#pragma once
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "jellybean/inference/backend.hpp"

namespace jellybean::inference {

class ModelRegistry {
public:
    void register_backend(const std::string& name, std::shared_ptr<IInferenceBackend> backend) {
        std::unique_lock lock(mu_);
        backends_[name] = std::move(backend);
    }

    std::shared_ptr<IInferenceBackend> get_backend(const std::string& name) const {
        std::shared_lock lock(mu_);
        auto it = backends_.find(name);
        if (it == backends_.end()) {
            return nullptr;
        }
        return it->second;
    }

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<IInferenceBackend>> backends_;
};

} // namespace jellybean::inference
