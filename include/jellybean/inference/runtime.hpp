#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jellybean/concurrency/mpmc_queue.hpp"
#include "jellybean/inference/backend.hpp"
#include "jellybean/telemetry/metrics.hpp"

namespace jellybean::inference {

struct RuntimeConfig {
    std::size_t worker_threads{1};
    std::size_t max_queue_size{1024};  // Note: Fixed to 1024 compile-time in Phase 3
    std::chrono::milliseconds enqueue_timeout{5};
    std::size_t max_batch_size{4};
    int64_t max_batch_delay_us{1000};  // 1ms flush delay

    static auto from_file(const std::string& path) -> RuntimeConfig;
};

class InferenceRuntime {
   public:
    explicit InferenceRuntime(RuntimeConfig cfg);
    ~InferenceRuntime();

    InferenceRuntime(const InferenceRuntime&) = delete;
    auto operator=(const InferenceRuntime&) -> InferenceRuntime& = delete;

    [[nodiscard]] auto register_model(const std::string& model_id,
                                      std::shared_ptr<IInferenceBackend> backend) -> bool;
    [[nodiscard]] auto infer(const InferenceRequest& req) -> InferenceResponse;
    [[nodiscard]] auto infer_async(const InferenceRequest& req) -> std::future<InferenceResponse>;
    void shutdown();

    [[nodiscard]] auto metrics() -> telemetry::RuntimeMetrics& {
        return metrics_;
    }
    [[nodiscard]] auto metrics() const -> const telemetry::RuntimeMetrics& {
        return metrics_;
    }
    [[nodiscard]] auto latency() -> telemetry::LatencyHistogram& {
        return latency_hist_;
    }
    [[nodiscard]] auto latency() const -> const telemetry::LatencyHistogram& {
        return latency_hist_;
    }

   private:
    struct Task {
        InferenceRequest req;
        std::promise<InferenceResponse> promise;
    };

    struct ModelQueue {
        jellybean::concurrency::MpmcQueue<Task, 1024> queue;
        std::atomic<bool> stopped{false};
        std::shared_ptr<IInferenceBackend> backend;
    };

    void worker_loop();

    RuntimeConfig cfg_;
    std::mutex models_mu_;
    std::unordered_map<std::string, std::shared_ptr<ModelQueue>> model_queues_;
    std::vector<std::thread> workers_;
    bool stopped_{false};

    telemetry::RuntimeMetrics metrics_;
    telemetry::LatencyHistogram latency_hist_;
};

}  // namespace jellybean::inference
