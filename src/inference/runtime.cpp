#include "jellybean/inference/runtime.hpp"

#include <chrono>
#include <utility>

namespace jellybean::inference {

InferenceRuntime::InferenceRuntime(RuntimeConfig cfg) : cfg_(cfg) {
    if (cfg_.worker_threads == 0) {
        cfg_.worker_threads = 1;
    }
    if (cfg_.max_queue_size == 0) {
        cfg_.max_queue_size = 1;
    }
    workers_.reserve(cfg_.worker_threads);
    for (std::size_t i = 0; i < cfg_.worker_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

InferenceRuntime::~InferenceRuntime() {
    shutdown();
}

bool InferenceRuntime::register_model(const std::string& model_id, std::shared_ptr<IInferenceBackend> backend) {
    if (!backend) {
        return false;
    }

    auto mq = std::make_shared<ModelQueue>();
    mq->capacity = cfg_.max_queue_size;
    mq->queue.resize(cfg_.max_queue_size);
    mq->backend = std::move(backend);

    std::lock_guard lock(models_mu_);
    auto [_, inserted] = model_queues_.emplace(model_id, std::move(mq));
    return inserted;
}

InferenceResponse InferenceRuntime::infer(const InferenceRequest& req) {
    std::shared_ptr<ModelQueue> mq;
    {
        std::lock_guard lock(models_mu_);
        auto it = model_queues_.find(req.model_id);
        if (it == model_queues_.end()) {
            InferenceResponse resp;
            resp.error = "model not registered: " + req.model_id;
            return resp;
        }
        mq = it->second;
    }

    auto deadline = std::chrono::steady_clock::now() + cfg_.enqueue_timeout;
    std::future<InferenceResponse> fut;
    {
        std::unique_lock lock(mq->mu);
        while (mq->size == mq->capacity && !mq->stopped) {
            if (mq->cv_has_space.wait_until(lock, deadline) == std::cv_status::timeout) {
                InferenceResponse resp;
                resp.error = "queue full timeout for model: " + req.model_id;
                return resp;
            }
        }
        if (mq->stopped) {
            InferenceResponse resp;
            resp.error = "runtime stopped";
            return resp;
        }

        Task& slot = mq->queue[mq->tail];
        slot.req = req;
        fut = slot.promise.get_future();
        mq->tail = (mq->tail + 1) % mq->capacity;
        ++mq->size;
    }

    mq->cv_has_data.notify_one();
    {
        std::lock_guard wake_lock(wake_mu_);
    }
    wake_cv_.notify_one();
    return fut.get();
}

void InferenceRuntime::shutdown() {
    {
        std::lock_guard lock(models_mu_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        for (auto& [_, mq] : model_queues_) {
            std::lock_guard qlock(mq->mu);
            mq->stopped = true;
            mq->cv_has_data.notify_all();
            mq->cv_has_space.notify_all();
        }
    }

    wake_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void InferenceRuntime::worker_loop() {
    for (;;) {
        std::vector<std::shared_ptr<ModelQueue>> queues;
        {
            std::lock_guard lock(models_mu_);
            if (stopped_) {
                return;
            }
            queues.reserve(model_queues_.size());
            for (const auto& [_, mq] : model_queues_) {
                queues.push_back(mq);
            }
        }

        bool did_work = false;
        for (auto& mq : queues) {
            Task task;
            bool popped = false;
            {
                std::lock_guard lock(mq->mu);
                if (mq->size > 0) {
                    task = std::move(mq->queue[mq->head]);
                    mq->queue[mq->head].promise = std::promise<InferenceResponse>{};
                    mq->head = (mq->head + 1) % mq->capacity;
                    --mq->size;
                    popped = true;
                }
            }
            if (!popped) {
                continue;
            }
            mq->cv_has_space.notify_one();

            InferenceResponse resp = mq->backend->infer(task.req);
            task.promise.set_value(std::move(resp));
            did_work = true;
        }

        if (!did_work) {
            std::unique_lock wake_lock(wake_mu_);
            wake_cv_.wait_for(wake_lock, std::chrono::milliseconds(1));
        }
    }
}

} // namespace jellybean::inference
