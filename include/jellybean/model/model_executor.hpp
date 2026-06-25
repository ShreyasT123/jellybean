#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <coroutine>

#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/inference/backend.hpp"
#include "jellybean/inference/types.hpp"
#include "jellybean/model/model_metadata.hpp"
#include "jellybean/telemetry/metrics.hpp"

namespace jellybean::reactor {
class Reactor;
}

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

    void enqueue(inference::InferenceRequest req, inference::InferenceResponse* resp, std::coroutine_handle<> h, uint64_t routing_ns = 0);
    
    void start();
    void stop();

    [[nodiscard]] auto metrics() -> telemetry::RuntimeMetrics& { return metrics_; }
    [[nodiscard]] auto metrics() const -> const telemetry::RuntimeMetrics& { return metrics_; }

    [[nodiscard]] auto latency_hist() -> telemetry::LatencyHistogram& { return latency_hist_; }
    [[nodiscard]] auto latency_hist() const -> const telemetry::LatencyHistogram& { return latency_hist_; }

   private:
    struct Task {
        inference::InferenceRequest req;
        inference::InferenceResponse* resp_ptr;
        std::coroutine_handle<> handle;
        jellybean::reactor::Reactor* reactor_ptr;
        std::chrono::steady_clock::time_point enqueue_time;
        uint64_t routing_ns{0};
    };

    void worker_loop();

    ModelMetadata* meta_;
    std::size_t num_workers_;
    
    // Hardcoded max size for lock-free queue capacity, power of 2
    jellybean::concurrency::MpmcQueue<Task, 1024> queue_;
    std::atomic<bool> stopped_{true};
    std::vector<std::thread> workers_;

    // Event-driven wakeup helper
    std::mutex cv_mu_;
    std::condition_variable cv_;
    std::atomic<std::intptr_t> queue_size_{0};

    telemetry::RuntimeMetrics metrics_;
    telemetry::LatencyHistogram latency_hist_;
};

}  // namespace jellybean::model
