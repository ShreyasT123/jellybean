#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jellybean/core/errors.hpp"
#include "jellybean/core/logging.hpp"
#include "jellybean/inference/runtime.hpp"
#include "jellybean/inference/torch_backend.hpp"
#include "jellybean/net/async_socket.hpp"
#include "jellybean/reactor/epoll_backend.hpp"
#include "jellybean/reactor/reactor.hpp"
#include "jellybean/scheduler/fiber.hpp"


using namespace jellybean::core;
using namespace jellybean::inference;
using namespace jellybean::reactor;
using namespace jellybean::net;
using namespace jellybean::scheduler;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    g_running = false;
    if (auto* r = Reactor::current()) {
        r->stop();
    }
}

struct ExtendedConfig {
    std::string model_id;
    std::string model_path;
    std::string host;
    int port{9000};
    std::vector<int64_t> input_shape;
    DeviceKind device{DeviceKind::Cpu};
};

auto load_extended_config(const std::string& path) -> ExtendedConfig {
    ExtendedConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        // Trim
        key.erase(0, key.find_first_not_of(" \t\r"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t\r"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        if (key == "model_id")
            cfg.model_id = val;
        else if (key == "model_path")
            cfg.model_path = val;
        else if (key == "host")
            cfg.host = val;
        else if (key == "port")
            cfg.port = std::stoi(val);
        else if (key == "device")
            cfg.device = (val == "cuda" ? DeviceKind::Cuda : DeviceKind::Cpu);
        else if (key == "input_shape") {
            std::stringstream ss(val);
            std::string item;
            while (std::getline(ss, item, ',')) {
                cfg.input_shape.push_back(std::stoll(item));
            }
        }
    }
    return cfg;
}

struct AsyncAcceptAwaitable {
    int epoll_fd;
    int listen_fd;

    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::coroutine_handle<> h) {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.ptr = h.address();
        if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1 && errno == EEXIST) {
            ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, listen_fd, &ev);
        }
    }
    int await_resume() noexcept {
        return ::accept(listen_fd, nullptr, nullptr);
    }
};

// Removed await_future as Task<T> await semantics are not safe for suspending inner tasks.

Task<> session(AsyncSocket sock, InferenceRuntime& runtime, ExtendedConfig config) {
    try {
        JELLY_LOG_INFO << "[SESSION] New client connected\n";
        while (g_running) {
            uint32_t input_elems = 0;
            ssize_t n = co_await sock.read(&input_elems, sizeof(input_elems));
            if (n <= 0) {
                JELLY_LOG_DEBUG << "[SESSION] Read input_elems failed: n=" << n << "\n";
                break;
            }
            JELLY_LOG_DEBUG << "[SESSION] Received input_elems: " << input_elems << "\n";

            // Cap memory usage to prevent silent OOM crashes
            const uint32_t MAX_ELEMS = 100000000;
            if (input_elems > MAX_ELEMS) {
                throw MemoryException("Input elements exceed maximum limit: " + std::to_string(input_elems));
            }

            std::vector<float> input_vec;
            try {
                input_vec.resize(input_elems);
            } catch (const std::bad_alloc& e) {
                throw MemoryException("Failed to allocate input buffer for " + std::to_string(input_elems) + " elements");
            }
            size_t bytes_to_read = input_elems * sizeof(float);
            size_t bytes_read = 0;
            while (bytes_read < bytes_to_read) {
                n = co_await sock.read(reinterpret_cast<char*>(input_vec.data()) + bytes_read,
                                       bytes_to_read - bytes_read);
                if (n <= 0) {
                    JELLY_LOG_DEBUG << "[SESSION] Read payload failed: n=" << n << " bytes_read=" << bytes_read << "/" << bytes_to_read << "\n";
                    co_return;
                }
                bytes_read += n;
            }
            JELLY_LOG_DEBUG << "[SESSION] Received payload: " << bytes_read << " bytes\n";

            std::vector<float> output_buf;
            try {
                output_buf.resize(input_elems);
            } catch (const std::bad_alloc& e) {
                throw MemoryException("Failed to allocate output buffer for " + std::to_string(input_elems) + " elements");
            }

            InferenceRequest req;
            req.model_id = config.model_id;
            req.shape = config.input_shape;
            req.input = input_vec;
            req.output_buffer = output_buf;
            req.device = config.device;

            JELLY_LOG_DEBUG << "[SESSION] Calling infer_async for model '" << config.model_id << "'\n";
            auto fut = runtime.infer_async(req);
            while (fut.wait_for(std::chrono::microseconds(10)) != std::future_status::ready) {
                co_await Yield{};
            }
            InferenceResponse resp = fut.get();
            JELLY_LOG_DEBUG << "[SESSION] Inference completed: ok=" << resp.ok << " latency_ns=" << resp.latency_ns << " output_elems=" << resp.output_elems_written << "\n";

            uint8_t status = resp.ok ? 1 : 0;
            n = co_await sock.write(&status, 1);
            JELLY_LOG_DEBUG << "[SESSION] Sent status: " << (int)status << " (wrote " << n << " bytes)\n";
            
            n = co_await sock.write(&resp.latency_ns, 8);
            JELLY_LOG_DEBUG << "[SESSION] Sent latency: " << resp.latency_ns << " (wrote " << n << " bytes)\n";

            uint32_t out_elems = resp.output_elems_written;
            n = co_await sock.write(&out_elems, 4);
            JELLY_LOG_DEBUG << "[SESSION] Sent out_elems: " << out_elems << " (wrote " << n << " bytes)\n";

            if (resp.ok && out_elems > 0) {
                n = co_await sock.write(output_buf.data(), out_elems * sizeof(float));
                JELLY_LOG_DEBUG << "[SESSION] Sent output payload: " << out_elems * sizeof(float) << " bytes (wrote " << n << " bytes)\n";
            }
        }
    } catch (const JellybeanException& e) {
        JELLY_LOG_ERROR << "[SESSION] JellybeanException: " << e.what() << "\n";
    } catch (const std::bad_alloc& e) {
        JELLY_LOG_ERROR << "[SESSION] Memory Error (bad_alloc): " << e.what() << "\n";
    } catch (const std::exception& e) {
        JELLY_LOG_ERROR << "[SESSION] Exception: " << e.what() << "\n";
    }
    JELLY_LOG_INFO << "[SESSION] Client disconnected\n";
    co_return;
}

Task<> accept_loop(int listen_fd, int epoll_fd, InferenceRuntime& runtime, ExtendedConfig config) {
    while (g_running) {
        int client_fd = co_await AsyncAcceptAwaitable{epoll_fd, listen_fd};
        if (client_fd >= 0) {
            JELLY_LOG_INFO << "[ACCEPT] New connection accepted, fd=" << client_fd << "\n";
            auto s = session(AsyncSocket(client_fd, epoll_fd), runtime, config);
            Reactor::current()->schedule(s.release());
        } else {
            JELLY_LOG_ERROR << "[ACCEPT] Accept failed, fd=" << client_fd << "\n";
        }
    }
    co_return;
}

int main(int argc, char* argv[]) {
    try {
        std::string config_path = "configs/server.config";
        if (argc > 1) config_path = argv[1];

        JELLY_LOG_INFO << "Jellybean Production Server starting [" << config_path << "]\n";
        JELLY_LOG_DEBUG << "[MAIN] Config path: " << config_path << "\n";

        auto runtime_cfg = RuntimeConfig::from_file(config_path);
        JELLY_LOG_DEBUG << "[MAIN] RuntimeConfig loaded\n";
        auto ext_cfg = load_extended_config(config_path);
        JELLY_LOG_DEBUG << "[MAIN] ExtendedConfig loaded: model_id='" << ext_cfg.model_id << "' host='" << ext_cfg.host << "' port=" << ext_cfg.port << "\n";

        auto backend = make_torch_backend();
        JELLY_LOG_DEBUG << "[MAIN] Torch backend created\n";
        JELLY_LOG_INFO << "Loading model '" << ext_cfg.model_id << "' from " << ext_cfg.model_path
                  << "...\n";
        if (!backend->load(ext_cfg.model_id, ext_cfg.model_path, ext_cfg.device)) {
            throw ServerException("Failed to load model '" + ext_cfg.model_id + "' from path '" + ext_cfg.model_path + "'");
        }
        JELLY_LOG_DEBUG << "[MAIN] Model loaded successfully\n";

        InferenceRuntime runtime(runtime_cfg);
        JELLY_LOG_DEBUG << "[MAIN] InferenceRuntime created\n";
        if (!runtime.register_model(ext_cfg.model_id, backend)) {
            throw ServerException("Failed to register model.");
        }
        JELLY_LOG_DEBUG << "[MAIN] Model registered successfully\n";

        auto epoll_backend = std::make_unique<EpollBackend>();
        int epoll_fd = epoll_backend->epoll_fd();
        Reactor reactor(std::move(epoll_backend));

        int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ext_cfg.port);
        inet_pton(AF_INET, ext_cfg.host.c_str(), &addr.sin_addr);

        if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            throw NetworkException("Failed to bind socket: " + std::string(strerror(errno)));
        }
        listen(listen_fd, SOMAXCONN);

        auto main_task = accept_loop(listen_fd, epoll_fd, runtime, ext_cfg);
        reactor.schedule(main_task.release());

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        JELLY_LOG_DEBUG << "[MAIN] Server ready on " << ext_cfg.host << ":" << ext_cfg.port << "\n";
        JELLY_LOG_INFO << "Server ready on " << ext_cfg.host << ":" << ext_cfg.port << "\n";

        while (g_running) {
            reactor.run();
        }

        JELLY_LOG_DEBUG << "[MAIN] Shutdown complete.\n";
        JELLY_LOG_INFO << "Shutdown complete.\n";
    } catch (const JellybeanException& e) {
        JELLY_LOG_ERROR << "Fatal Jellybean Error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        JELLY_LOG_ERROR << "Fatal System Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
