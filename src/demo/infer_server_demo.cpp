#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jellybean/inference/runtime.hpp"
#include "jellybean/inference/torch_backend.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
static constexpr socket_t invalid_socket = INVALID_SOCKET;
static int close_socket(socket_t s) { return closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t invalid_socket = -1;
static int close_socket(socket_t s) { return close(s); }
#endif

namespace {
struct ServerConfig {
    std::string model_id = "decoder_transformer";
    std::string model_path = "model.pt";
    std::string host = "127.0.0.1";
    uint16_t port = 9000;
    std::vector<int64_t> input_shape = {1, 128, 512};
    uint32_t expected_output_elems = 512;
    std::size_t workers = 4;
    std::size_t queue_size = 256;
    int enqueue_timeout_ms = 20;
    int max_connections = 32;
    int max_requests = 80;
    std::string log_file = "infer_server.log";
};

class Logger {
public:
    explicit Logger(std::string path) : out_(std::move(path), std::ios::app) {}

    void log(const std::string& msg) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        std::ostringstream ts;
        ts << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        std::lock_guard lock(mu_);
        std::cout << "[" << ts.str() << "] " << msg << "\n";
        if (out_.is_open()) {
            out_ << "[" << ts.str() << "] " << msg << "\n";
            out_.flush();
        }
    }

private:
    std::mutex mu_;
    std::ofstream out_;
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool parse_shape(const std::string& s, std::vector<int64_t>* out) {
    std::vector<int64_t> dims;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) return false;
        const long long v = std::stoll(tok);
        if (v <= 0) return false;
        dims.push_back(static_cast<int64_t>(v));
    }
    if (dims.empty()) return false;
    *out = std::move(dims);
    return true;
}

bool parse_u16(const std::string& s, uint16_t* out) {
    try {
        const unsigned long v = std::stoul(s);
        if (v > 65535UL) return false;
        *out = static_cast<uint16_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_u32(const std::string& s, uint32_t* out) {
    try {
        const unsigned long v = std::stoul(s);
        *out = static_cast<uint32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_size(const std::string& s, std::size_t* out) {
    try {
        const unsigned long long v = std::stoull(s);
        *out = static_cast<std::size_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_i32(const std::string& s, int* out) {
    try {
        *out = std::stoi(s);
        return true;
    } catch (...) {
        return false;
    }
}

uint32_t num_elems(const std::vector<int64_t>& shape) {
    uint64_t n = 1;
    for (const auto d : shape) n *= static_cast<uint64_t>(d);
    return static_cast<uint32_t>(n);
}

ServerConfig load_config(const std::string& path) {
    ServerConfig cfg;
    std::ifstream in(path);
    if (!in.is_open()) return cfg;

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        kv[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
    }

    if (kv.count("model_id")) cfg.model_id = kv["model_id"];
    if (kv.count("model_path")) cfg.model_path = kv["model_path"];
    if (kv.count("host")) cfg.host = kv["host"];
    if (kv.count("port")) parse_u16(kv["port"], &cfg.port);
    if (kv.count("input_shape")) parse_shape(kv["input_shape"], &cfg.input_shape);
    if (kv.count("expected_output_elems")) parse_u32(kv["expected_output_elems"], &cfg.expected_output_elems);
    if (kv.count("workers")) parse_size(kv["workers"], &cfg.workers);
    if (kv.count("queue_size")) parse_size(kv["queue_size"], &cfg.queue_size);
    if (kv.count("enqueue_timeout_ms")) parse_i32(kv["enqueue_timeout_ms"], &cfg.enqueue_timeout_ms);
    if (kv.count("max_connections")) parse_i32(kv["max_connections"], &cfg.max_connections);
    if (kv.count("max_requests")) parse_i32(kv["max_requests"], &cfg.max_requests);
    if (kv.count("log_file")) cfg.log_file = kv["log_file"];
    return cfg;
}

bool init_sockets() {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanup_sockets() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool send_all(socket_t fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int n = send(fd, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_all(socket_t fd, char* data, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        const int n = recv(fd, data + recvd, static_cast<int>(len - recvd), 0);
        if (n <= 0) return false;
        recvd += static_cast<size_t>(n);
    }
    return true;
}

bool parse_ipv4_addr(const std::string& host, in_addr* out) {
#if defined(_WIN32)
    return InetPtonA(AF_INET, host.c_str(), out) == 1;
#else
    return inet_pton(AF_INET, host.c_str(), out) == 1;
#endif
}
} // namespace

int main(int argc, char** argv) {
    const std::string config_path = (argc > 1) ? argv[1] : "server.config";
    const ServerConfig cfg = load_config(config_path);
    Logger logger(cfg.log_file);

    std::ostringstream shape_ss;
    for (size_t i = 0; i < cfg.input_shape.size(); ++i) {
        if (i) shape_ss << ",";
        shape_ss << cfg.input_shape[i];
    }
    logger.log("==============================================");
    logger.log(" Jellybean Inference Server (Triton-style demo)");
    logger.log(" config=" + config_path + " model=" + cfg.model_path + " host=" + cfg.host + ":" + std::to_string(cfg.port));
    logger.log(" input_shape=" + shape_ss.str() + " expected_output_elems=" + std::to_string(cfg.expected_output_elems));
    logger.log("==============================================");

    if (!init_sockets()) {
        logger.log("socket init failed");
        return 1;
    }

    jellybean::inference::RuntimeConfig rt_cfg;
    rt_cfg.worker_threads = cfg.workers;
    rt_cfg.max_queue_size = cfg.queue_size;
    rt_cfg.enqueue_timeout = std::chrono::milliseconds(cfg.enqueue_timeout_ms);
    jellybean::inference::InferenceRuntime runtime(rt_cfg);

    auto backend = jellybean::inference::make_torch_backend();
    if (!backend->load(cfg.model_id, cfg.model_path, jellybean::inference::DeviceKind::Cpu)) {
        logger.log("model load failed: " + cfg.model_path);
        cleanup_sockets();
        return 2;
    }
    if (!runtime.register_model(cfg.model_id, backend)) {
        logger.log("model register failed: " + cfg.model_id);
        cleanup_sockets();
        return 3;
    }

    const uint32_t expected_input_elems = num_elems(cfg.input_shape);
    std::atomic<int> handled_requests{0};
    std::atomic<bool> stop{false};

    socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == invalid_socket) {
        logger.log("listen socket creation failed");
        cleanup_sockets();
        return 4;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (!parse_ipv4_addr(cfg.host, &addr.sin_addr)) {
        logger.log("invalid host ip: " + cfg.host);
        close_socket(listen_fd);
        cleanup_sockets();
        return 5;
    }
    addr.sin_port = htons(cfg.port);
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger.log("bind failed on " + cfg.host + ":" + std::to_string(cfg.port));
        close_socket(listen_fd);
        cleanup_sockets();
        return 5;
    }
    if (listen(listen_fd, cfg.max_connections) != 0) {
        logger.log("listen failed");
        close_socket(listen_fd);
        cleanup_sockets();
        return 6;
    }
    logger.log("server started and listening");

    std::vector<std::thread> conn_threads;
    conn_threads.reserve(static_cast<size_t>(cfg.max_connections));
    while (!stop.load(std::memory_order_acquire)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        #if defined(_WIN32)
        const int sel = select(0, &readfds, nullptr, nullptr, &tv);
        #else
        const int sel = select(listen_fd + 1, &readfds, nullptr, nullptr, &tv);
        #endif
        if (sel <= 0) continue;

        socket_t conn = accept(listen_fd, nullptr, nullptr);
        if (conn == invalid_socket) continue;
        conn_threads.emplace_back([&, conn]() {
            std::vector<float> input(expected_input_elems);
            while (!stop.load(std::memory_order_acquire)) {
                uint32_t input_elems = 0;
                if (!recv_all(conn, reinterpret_cast<char*>(&input_elems), sizeof(input_elems))) break;
                if (input_elems != expected_input_elems) {
                    logger.log("invalid request input elems=" + std::to_string(input_elems) +
                               " expected=" + std::to_string(expected_input_elems));
                    const uint8_t ok = 0;
                    const uint64_t latency = 0;
                    const uint32_t out_elems = 0;
                    send_all(conn, reinterpret_cast<const char*>(&ok), sizeof(ok));
                    send_all(conn, reinterpret_cast<const char*>(&latency), sizeof(latency));
                    send_all(conn, reinterpret_cast<const char*>(&out_elems), sizeof(out_elems));
                    break;
                }
                if (!recv_all(conn, reinterpret_cast<char*>(input.data()), input.size() * sizeof(float))) break;

                jellybean::inference::InferenceRequest req;
                req.model_id = cfg.model_id;
                req.shape = cfg.input_shape;
                req.input = input;
                auto resp = runtime.infer(req);

                uint8_t ok = 0;
                uint64_t latency = resp.latency_ns;
                uint32_t out_elems = 0;
                if (resp.ok && resp.output.size() == cfg.expected_output_elems) {
                    ok = 1;
                    out_elems = static_cast<uint32_t>(resp.output.size());
                } else {
                    logger.log("invalid inference response or output size mismatch");
                }

                if (!send_all(conn, reinterpret_cast<const char*>(&ok), sizeof(ok))) break;
                if (!send_all(conn, reinterpret_cast<const char*>(&latency), sizeof(latency))) break;
                if (!send_all(conn, reinterpret_cast<const char*>(&out_elems), sizeof(out_elems))) break;
                if (ok == 1 && out_elems > 0) {
                    if (!send_all(conn, reinterpret_cast<const char*>(resp.output.data()),
                                  resp.output.size() * sizeof(float)))
                        break;
                }

                const int done = handled_requests.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (cfg.max_requests > 0 && done >= cfg.max_requests) {
                    stop.store(true, std::memory_order_release);
                    break;
                }
            }
            close_socket(conn);
        });
    }

    close_socket(listen_fd);
#if defined(_WIN32)
    socket_t poke = ::socket(AF_INET, SOCK_STREAM, 0);
    if (poke != invalid_socket) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        parse_ipv4_addr(cfg.host, &dst.sin_addr);
        dst.sin_port = htons(cfg.port);
        connect(poke, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        close_socket(poke);
    }
#endif
    for (auto& t : conn_threads) {
        if (t.joinable()) t.join();
    }

    logger.log("server stopping. handled_requests=" + std::to_string(handled_requests.load()));
    cleanup_sockets();
    return 0;
}
