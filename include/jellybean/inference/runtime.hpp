#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jellybean/inference/backend.hpp"

namespace jellybean::inference {

struct RuntimeConfig {
    std::size_t worker_threads{1};
    std::size_t max_queue_size{1024};
    std::chrono::milliseconds enqueue_timeout{5};
};

class InferenceRuntime {
public:
    explicit InferenceRuntime(RuntimeConfig cfg);
    ~InferenceRuntime();

    InferenceRuntime(const InferenceRuntime&) = delete;
    InferenceRuntime& operator=(const InferenceRuntime&) = delete;

    bool register_model(const std::string& model_id, std::shared_ptr<IInferenceBackend> backend);
    InferenceResponse infer(const InferenceRequest& req);
    void shutdown();

private:
    struct Task {
        InferenceRequest req;
        std::promise<InferenceResponse> promise;
    };

    struct ModelQueue {
        std::mutex mu;
        std::condition_variable cv_has_data;
        std::condition_variable cv_has_space;
        std::vector<Task> queue;
        std::size_t head{0};
        std::size_t tail{0};
        std::size_t size{0};
        std::size_t capacity{0};
        bool stopped{false};
        std::shared_ptr<IInferenceBackend> backend;
    };

    void worker_loop();

    RuntimeConfig cfg_;
    std::mutex models_mu_;
    std::unordered_map<std::string, std::shared_ptr<ModelQueue>> model_queues_;
    std::vector<std::thread> workers_;
    std::mutex wake_mu_;
    std::condition_variable wake_cv_;
    bool stopped_{false};
};

} // namespace jellybean::inference
