#include "jellybean/telemetry/metrics_export.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <cstring>
#include <iostream>
#include "jellybean/core/logging.hpp"

namespace jellybean::telemetry {

auto serialize_metrics_json(const jellybean::inference::InferenceRuntime& runtime) -> std::string {
    auto all = runtime.get_all_metrics();
    std::ostringstream ss;
    ss << "{\n";
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& m = all[i];
        ss << "  \"" << m.model_id << "\": {\n";
        ss << "    \"requests_received\": " << m.metrics.requests_received << ",\n";
        ss << "    \"requests_completed\": " << m.metrics.requests_completed << ",\n";
        ss << "    \"requests_rejected\": " << m.metrics.requests_rejected << ",\n";
        ss << "    \"queue_timeouts\": " << m.metrics.queue_timeouts << ",\n";
        ss << "    \"latency_p50_ns\": " << m.p50_ns << ",\n";
        ss << "    \"latency_p95_ns\": " << m.p95_ns << ",\n";
        ss << "    \"latency_p99_ns\": " << m.p99_ns << "\n";
        ss << "  }";
        if (i + 1 < all.size()) {
            ss << ",";
        }
        ss << "\n";
    }
    ss << "}\n";
    return ss.str();
}

MetricsServer::MetricsServer(const jellybean::inference::InferenceRuntime& runtime, int port)
    : runtime_(runtime), port_(port) {}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    bool expected = true;
    if (stopped_.compare_exchange_strong(expected, false)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            stopped_.store(true);
            return;
        }

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            stopped_.store(true);
            return;
        }

        if (::listen(listen_fd_, 10) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            stopped_.store(true);
            return;
        }

        thread_ = std::thread([this] { accept_loop(); });
    }
}

void MetricsServer::stop() {
    bool expected = false;
    if (stopped_.compare_exchange_strong(expected, true)) {
        if (listen_fd_ >= 0) {
            // shutdown and close to unblock any pending accept
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

void MetricsServer::accept_loop() {
    while (!stopped_.load()) {
        int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (stopped_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::string json = serialize_metrics_json(runtime_);
        ssize_t written = 0;
        ssize_t total = json.size();
        while (written < total && !stopped_.load()) {
            ssize_t n = ::write(client_fd, json.data() + written, total - written);
            if (n <= 0) break;
            written += n;
        }
        ::close(client_fd);
    }
}

} // namespace jellybean::telemetry
