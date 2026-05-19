#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/inference/backend.hpp"
#include "jellybean/inference/types.hpp"
#include "jellybean/model/model_metadata.hpp"
#include "jellybean/telemetry/metrics.hpp"

namespace jellybean::model {

/**
 * @brief Independent execution context for a single model.
 *
 * Owns a dedicated lock-free queue and worker thread pool for batching and executing
 * inference requests against a specific model backend.
 * This decouples models from each other, preventing slow models from blocking
 * fast ones and allowing heterogeneous batching policies.
 */
class ModelExecutor {
   public:
    explicit ModelExecutor(ModelMetadata* meta, std::size_t num_workers = 1);
    ~ModelExecutor();

    ModelExecutor(const ModelExecutor&) = delete;
    auto operator=(const ModelExecutor&) -> ModelExecutor& = delete;

    [[nodiscard]] auto infer_async(const inference::InferenceRequest& req) -> std::future<inference::InferenceResponse>;
    
    void start();
    void stop();

    [[nodiscard]] auto metrics() -> telemetry::RuntimeMetrics& { return metrics_; }
    [[nodiscard]] auto metrics() const -> const telemetry::RuntimeMetrics& { return metrics_; }

    [[nodiscard]] auto latency_hist() -> telemetry::LatencyHistogram& { return latency_hist_; }
    [[nodiscard]] auto latency_hist() const -> const telemetry::LatencyHistogram& { return latency_hist_; }

   private:
    struct Task {
        inference::InferenceRequest req;
        std::promise<inference::InferenceResponse> promise;
    };

    void worker_loop();

    ModelMetadata* meta_;
    std::size_t num_workers_;
    
    // Hardcoded max size for lock-free queue capacity, power of 2
    jellybean::concurrency::MpmcQueue<Task, 1024> queue_;
    std::atomic<bool> stopped_{true};
    std::vector<std::thread> workers_;

    telemetry::RuntimeMetrics metrics_;
    telemetry::LatencyHistogram latency_hist_;
};

}  // namespace jellybean::model
