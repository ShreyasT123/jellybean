#include "jellybean/model/model_executor.hpp"

#include <chrono>

#include "jellybean/concurrency/backoff.hpp"

namespace jellybean::model {

ModelExecutor::ModelExecutor(ModelMetadata* meta, std::size_t num_workers)
    : meta_(meta), num_workers_(num_workers > 0 ? num_workers : 1) {}

ModelExecutor::~ModelExecutor() {
    stop();
}

void ModelExecutor::start() {
    bool expected = true;
    if (stopped_.compare_exchange_strong(expected, false)) {
        workers_.reserve(num_workers_);
        for (std::size_t i = 0; i < num_workers_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }
}

void ModelExecutor::stop() {
    bool expected = false;
    if (stopped_.compare_exchange_strong(expected, true)) {
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
}

auto ModelExecutor::infer_async(const inference::InferenceRequest& req) -> std::future<inference::InferenceResponse> {
    metrics_.requests_received.fetch_add(1, std::memory_order_relaxed);

    Task slot;
    slot.req = req;
    auto fut = slot.promise.get_future();

    if (!meta_ || !meta_->is_ready() || !meta_->backend) {
        inference::InferenceResponse resp;
        resp.error = "model not ready: " + req.model_id;
        slot.promise.set_value(std::move(resp));
        return fut;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5); // hardcoded enqueue timeout for now
    
    while (!stopped_.load(std::memory_order_acquire)) {
        Task temp = std::move(slot);
        if (queue_.try_push(std::move(temp))) {
            return fut;
        }
        slot = std::move(temp); // NOLINT(bugprone-use-after-move)
        
        if (std::chrono::steady_clock::now() > deadline) {
            metrics_.queue_timeouts.fetch_add(1, std::memory_order_relaxed);
            inference::InferenceResponse resp;
            resp.error = "queue full timeout for model: " + req.model_id;
            slot.promise.set_value(std::move(resp));
            return fut;
        }
        std::this_thread::yield();
    }

    inference::InferenceResponse resp;
    resp.error = "executor stopped";
    slot.promise.set_value(std::move(resp));
    return fut;
}

void ModelExecutor::worker_loop() {
    while (!stopped_.load(std::memory_order_acquire)) {
        std::vector<Task> batch;
        batch.reserve(meta_->config.max_batch_size);
        auto first_time = std::chrono::steady_clock::now();
        concurrency::Backoff backoff;
        
        while (batch.size() < meta_->config.max_batch_size && !stopped_.load(std::memory_order_acquire)) {
            auto task_opt = queue_.try_pop();
            if (task_opt) {
                batch.push_back(std::move(*task_opt));
                backoff.reset();
            } else {
                if (batch.empty()) {
                    break;
                }
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - first_time).count();
                if (elapsed >= meta_->config.max_batch_delay_us) {
                    break;
                }
                backoff.pause();
            }
        }

        if (!batch.empty() && meta_->is_ready() && meta_->backend) {
            std::vector<inference::InferenceRequest> reqs;
            reqs.reserve(batch.size());
            for (const auto& t : batch) reqs.push_back(t.req);

            auto resps = meta_->backend->infer_batch(reqs);

            for (size_t i = 0; i < batch.size(); ++i) {
                latency_hist_.record(resps[i].latency_ns);
                if (resps[i].ok) {
                    metrics_.requests_completed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    metrics_.requests_rejected.fetch_add(1, std::memory_order_relaxed);
                }
                batch[i].promise.set_value(std::move(resps[i]));
            }
        } else if (batch.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else {
            // Backend disappeared or model stopped during batching
            for (size_t i = 0; i < batch.size(); ++i) {
                inference::InferenceResponse resp;
                resp.error = "backend unavailable";
                batch[i].promise.set_value(std::move(resp));
            }
        }
    }
}

}  // namespace jellybean::model
