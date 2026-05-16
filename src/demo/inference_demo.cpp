#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "jellybean/inference/runtime.hpp"
#include "jellybean/inference/torch_backend.hpp"
#include "jellybean/inference/types.hpp"

namespace {
uint64_t percentile(std::vector<uint64_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}
}

int main() {
    const std::string model_path = "model.pt";
    const int concurrency = 4;
    const int requests_per_thread = 100;

    jellybean::inference::RuntimeConfig cfg;
    cfg.worker_threads = 4;
    cfg.max_queue_size = 256;
    cfg.enqueue_timeout = std::chrono::milliseconds(10);
    jellybean::inference::InferenceRuntime runtime(cfg);

    auto backend = jellybean::inference::make_torch_backend();
    if (!backend->load("decoder_transformer", model_path, jellybean::inference::DeviceKind::Cpu)) {
        std::cerr << "model load failed: " << model_path << "\n";
        return 1;
    }
    if (!runtime.register_model("decoder_transformer", backend)) {
        std::cerr << "model register failed\n";
        return 2;
    }

    const int samples = concurrency * requests_per_thread;
    std::vector<uint64_t> lat_ns;
    lat_ns.reserve(samples);
    std::mutex lat_mu;

    std::atomic<int> ok_count{0};
    std::atomic<int> fail_count{0};
    const auto wall_start = std::chrono::steady_clock::now();

    std::vector<std::thread> clients;
    clients.reserve(concurrency);
    for (int t = 0; t < concurrency; ++t) {
        clients.emplace_back([&, t] {
            for (int i = 0; i < requests_per_thread; ++i) {
                jellybean::inference::InferenceRequest req;
                req.model_id = "decoder_transformer";
                req.shape = {1, 128, 512};
                req.input.resize(1 * 128 * 512);
                for (size_t j = 0; j < req.input.size(); ++j) {
                    req.input[j] = static_cast<float>((j + t + i) % 97) * 0.01f;
                }

                auto resp = runtime.infer(req);
                if (!resp.ok) {
                    ++fail_count;
                    continue;
                }
                ++ok_count;
                std::lock_guard lock(lat_mu);
                lat_ns.push_back(resp.latency_ns);
            }
        });
    }

    for (auto& c : clients) {
        c.join();
    }
    const auto wall_end = std::chrono::steady_clock::now();

    if (lat_ns.empty()) {
        std::cerr << "all requests failed\n";
        return 3;
    }

    const auto wall_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count());
    const double throughput_rps = (static_cast<double>(ok_count.load()) * 1e9) / wall_ns;
    const uint64_t avg = std::accumulate(lat_ns.begin(), lat_ns.end(), 0ULL) / lat_ns.size();

    std::cout << "inference server demo ok\n";
    std::cout << "model: " << model_path << "\n";
    std::cout << "workers: " << cfg.worker_threads << " queue_cap: " << cfg.max_queue_size << "\n";
    std::cout << "clients: " << concurrency << " requests: " << samples << "\n";
    std::cout << "success: " << ok_count.load() << " failed: " << fail_count.load() << "\n";
    std::cout << "throughput req/s: " << static_cast<uint64_t>(throughput_rps) << "\n";
    std::cout << "latency ns: avg=" << avg
              << " p50=" << percentile(lat_ns, 0.50)
              << " p99=" << percentile(lat_ns, 0.99) << "\n";
    return 0;
}
