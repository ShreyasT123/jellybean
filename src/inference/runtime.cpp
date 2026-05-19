#include "jellybean/inference/runtime.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>
#include "jellybean/concurrency/backoff.hpp"

namespace jellybean::inference {

RuntimeConfig RuntimeConfig::from_file(const std::string& path) {
    RuntimeConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) {
        return cfg;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        // Trim possible whitespace including \r from Windows line endings
        key.erase(0, key.find_first_not_of(" \t\r"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t\r"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        if (key == "worker_threads") cfg.worker_threads = std::stoul(val);
        else if (key == "max_queue_size") cfg.max_queue_size = std::stoul(val);
        else if (key == "enqueue_timeout_ms") cfg.enqueue_timeout = std::chrono::milliseconds(std::stoul(val));
        else if (key == "max_batch_size") cfg.max_batch_size = std::stoul(val);
        else if (key == "max_batch_delay_us") cfg.max_batch_delay_us = std::stoll(val);
    }
    return cfg;
}

InferenceRuntime::InferenceRuntime(RuntimeConfig cfg) : cfg_(cfg) {
    if (cfg_.worker_threads == 0) {
        cfg_.worker_threads = 1;
    }
    workers_.reserve(cfg_.worker_threads);
    for (std::size_t i = 0; i < cfg_.worker_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

InferenceRuntime::~InferenceRuntime() {
    shutdown();
}

auto InferenceRuntime::register_model(const std::string& model_id, std::shared_ptr<IInferenceBackend> backend) -> bool {
    if (!backend) {
        return false;
    }

    auto mq = std::make_shared<ModelQueue>();
    mq->backend = std::move(backend);

    std::lock_guard lock(models_mu_);
    auto [_, inserted] = model_queues_.emplace(model_id, std::move(mq));
    return inserted;
}

auto InferenceRuntime::infer(const InferenceRequest& req) -> InferenceResponse {
    auto fut = infer_async(req);
    return fut.get();
}

auto InferenceRuntime::infer_async(const InferenceRequest& req) -> std::future<InferenceResponse> {
    metrics_.requests_received.fetch_add(1, std::memory_order_relaxed);

    std::shared_ptr<ModelQueue> mq;
    {
        std::lock_guard lock(models_mu_);
        auto it = model_queues_.find(req.model_id);
        if (it == model_queues_.end()) {
            std::promise<InferenceResponse> p;
            InferenceResponse resp;
            resp.error = "model not registered: " + req.model_id;
            p.set_value(std::move(resp));
            return p.get_future();
        }
        mq = it->second;
    }

    auto deadline = std::chrono::steady_clock::now() + cfg_.enqueue_timeout;
    Task slot;
    slot.req = req;
    auto fut = slot.promise.get_future();

    while (!mq->stopped.load(std::memory_order_acquire)) {
        Task temp = std::move(slot);
        if (mq->queue.try_push(std::move(temp))) {
            return fut;
        }
        slot = std::move(temp); // NOLINT(bugprone-use-after-move)
        
        if (std::chrono::steady_clock::now() > deadline) {
            metrics_.queue_timeouts.fetch_add(1, std::memory_order_relaxed);
            std::promise<InferenceResponse> p;
            InferenceResponse resp;
            resp.error = "queue full timeout for model: " + req.model_id;
            p.set_value(std::move(resp));
            return p.get_future();
        }
        // Lock-free queue is full, backoff slightly to avoid 100% CPU spin
        std::this_thread::yield();
    }

    std::promise<InferenceResponse> p;
    InferenceResponse resp;
    resp.error = "runtime stopped";
    p.set_value(std::move(resp));
    return p.get_future();
}

void InferenceRuntime::shutdown() {
    {
        std::lock_guard lock(models_mu_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        for (auto& [_, mq] : model_queues_) {
            mq->stopped.store(true, std::memory_order_release);
        }
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void InferenceRuntime::worker_loop() {
    std::vector<std::shared_ptr<ModelQueue>> queues;
    constexpr size_t INITIAL_QUEUE_CAPACITY = 16;
    queues.reserve(INITIAL_QUEUE_CAPACITY);
    for (;;) {
        queues.clear();
        {
            std::lock_guard lock(models_mu_);
            if (stopped_) {
                return;
            }
            for (const auto& [_, mq] : model_queues_) {
                queues.push_back(mq);
            }
        }

        bool did_work = false;
        for (auto& mq : queues) {
            if (mq->stopped.load(std::memory_order_acquire)) continue;

            std::vector<Task> batch;
            batch.reserve(cfg_.max_batch_size);
            auto first_time = std::chrono::steady_clock::now();
            concurrency::Backoff backoff;
            
            while (batch.size() < cfg_.max_batch_size) {
                auto task_opt = mq->queue.try_pop();
                if (task_opt) {
                    batch.push_back(std::move(*task_opt));
                    backoff.reset();
                } else {
                    if (batch.empty()) {
                        break; // Queue is empty, check next queue
                    }
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - first_time).count();
                    if (elapsed >= cfg_.max_batch_delay_us) {
                        break; // Flush batch due to timeout
                    }
                    backoff.pause();
                }
            }

            if (!batch.empty()) {
                std::vector<InferenceRequest> reqs;
                reqs.reserve(batch.size());
                for (const auto& t : batch) reqs.push_back(t.req);

                auto resps = mq->backend->infer_batch(reqs);

                for (size_t i = 0; i < batch.size(); ++i) {
                    latency_hist_.record(resps[i].latency_ns);
                    if (resps[i].ok) {
                        metrics_.requests_completed.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        metrics_.requests_rejected.fetch_add(1, std::memory_order_relaxed);
                    }
                    batch[i].promise.set_value(std::move(resps[i]));
                }
                did_work = true;
            }
        }

        if (!did_work) {
            // No work available in any queue. Since we dropped the heavy condition_variable,
            // we use a micro-sleep to prevent burning CPU cycles in an empty lock-free loop.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

} // namespace jellybean::inference
