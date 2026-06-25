#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/inference/backend.hpp"
#include "jellybean/telemetry/metrics.hpp"

#include "jellybean/model/model_executor.hpp"
#include <coroutine>

namespace jellybean::inference {

class InferenceRuntime;

struct RuntimeConfig {
    std::size_t worker_threads{1};
    static auto from_file(const std::string& path) -> RuntimeConfig;
};

class InferenceRuntime {
   public:
    explicit InferenceRuntime(RuntimeConfig cfg);
    ~InferenceRuntime();

    InferenceRuntime(const InferenceRuntime&) = delete;
    auto operator=(const InferenceRuntime&) -> InferenceRuntime& = delete;

    [[nodiscard]] auto register_model(const std::string& model_id,
                                      jellybean::model::ModelMetadata* meta) -> bool;
    
    [[nodiscard]] auto unregister_model(const std::string& model_id) -> bool;

    void enqueue(InferenceRequest req, InferenceResponse* resp, std::coroutine_handle<> h);
    
    [[nodiscard]] auto get_all_metrics() const -> std::vector<jellybean::telemetry::ModelExecutorMetrics>;

    void shutdown();

   private:
    RuntimeConfig cfg_;
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<jellybean::model::ModelExecutor>> executors_;
    bool stopped_{false};
};

struct InferenceAwaitable {
    InferenceRuntime& runtime;
    InferenceRequest req;
    InferenceResponse resp;

    InferenceAwaitable(InferenceRuntime& rt, InferenceRequest request)
        : runtime(rt), req(std::move(request)) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h);

    InferenceResponse await_resume() {
        return std::move(resp);
    }
};

}  // namespace jellybean::inference
