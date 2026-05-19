#include "jellybean/inference/runtime.hpp"

#include <fstream>
#include <string>

namespace jellybean::inference {

RuntimeConfig RuntimeConfig::from_file(const std::string& path) {
    RuntimeConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        key.erase(0, key.find_first_not_of(" \t\r"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t\r"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        if (key == "worker_threads") cfg.worker_threads = std::stoul(val);
    }
    return cfg;
}

InferenceRuntime::InferenceRuntime(RuntimeConfig cfg) : cfg_(cfg) {}

InferenceRuntime::~InferenceRuntime() {
    shutdown();
}

auto InferenceRuntime::register_model(const std::string& model_id, jellybean::model::ModelMetadata* meta) -> bool {
    if (!meta || !meta->is_ready()) return false;

    auto executor = std::make_shared<jellybean::model::ModelExecutor>(meta, cfg_.worker_threads);
    executor->start();

    std::lock_guard lock(mu_);
    auto [_, inserted] = executors_.emplace(model_id, std::move(executor));
    return inserted;
}

auto InferenceRuntime::unregister_model(const std::string& model_id) -> bool {
    std::shared_ptr<jellybean::model::ModelExecutor> executor;
    {
        std::lock_guard lock(mu_);
        auto it = executors_.find(model_id);
        if (it == executors_.end()) return false;
        executor = std::move(it->second);
        executors_.erase(it);
    }
    // Stopping executor outside the lock to prevent deadlock
    executor->stop();
    return true;
}

auto InferenceRuntime::infer(const InferenceRequest& req) -> InferenceResponse {
    return infer_async(req).get();
}

auto InferenceRuntime::infer_async(const InferenceRequest& req) -> std::future<InferenceResponse> {
    std::shared_ptr<jellybean::model::ModelExecutor> executor;
    {
        std::lock_guard lock(mu_);
        if (stopped_) {
            std::promise<InferenceResponse> p;
            InferenceResponse resp;
            resp.error = "runtime stopped";
            p.set_value(std::move(resp));
            return p.get_future();
        }
        auto it = executors_.find(req.model_id);
        if (it != executors_.end()) {
            executor = it->second;
        }
    }

    if (!executor) {
        std::promise<InferenceResponse> p;
        InferenceResponse resp;
        resp.error = "model not registered in runtime: " + req.model_id;
        p.set_value(std::move(resp));
        return p.get_future();
    }

    return executor->infer_async(req);
}

void InferenceRuntime::shutdown() {
    std::unordered_map<std::string, std::shared_ptr<jellybean::model::ModelExecutor>> to_stop;
    {
        std::lock_guard lock(mu_);
        if (stopped_) return;
        stopped_ = true;
        to_stop = std::move(executors_);
    }
    for (auto& [_, executor] : to_stop) {
        executor->stop();
    }
}

}  // namespace jellybean::inference
