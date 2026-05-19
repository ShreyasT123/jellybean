#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "jellybean/inference/runtime.hpp"

namespace jellybean::telemetry {

/**
 * @brief Expose a JSON metrics snapshot of all registered models.
 */
auto serialize_metrics_json(const jellybean::inference::InferenceRuntime& runtime) -> std::string;

/**
 * @brief Simple telemetry TCP server to listen on port 9001 and dump JSON metrics on connect.
 */
class MetricsServer {
   public:
    MetricsServer(const jellybean::inference::InferenceRuntime& runtime, int port);
    ~MetricsServer();

    MetricsServer(const MetricsServer&) = delete;
    auto operator=(const MetricsServer&) -> MetricsServer& = delete;

    void start();
    void stop();

   private:
    void accept_loop();

    const jellybean::inference::InferenceRuntime& runtime_;
    int port_;
    std::thread thread_;
    std::atomic<bool> stopped_{true};
    int listen_fd_{-1};
};

} // namespace jellybean::telemetry
