#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "jellybean/actor/mailbox.hpp"
#include "jellybean/reactor/reactor.hpp"

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

using steady_clock_t = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;

uint64_t percentile_ns(std::vector<uint64_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
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

int run_tcp_echo_roundtrip() {
    std::promise<uint16_t> port_ready;
    std::future<uint16_t> port_future = port_ready.get_future();
    std::atomic<bool> ok{false};

    std::thread server([&]() {
        socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == invalid_socket) return;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close_socket(listen_fd);
            return;
        }
        if (listen(listen_fd, 1) != 0) {
            close_socket(listen_fd);
            return;
        }

        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
            close_socket(listen_fd);
            return;
        }
        port_ready.set_value(ntohs(bound.sin_port));

        socket_t conn = accept(listen_fd, nullptr, nullptr);
        if (conn == invalid_socket) {
            close_socket(listen_fd);
            return;
        }

        std::array<char, 128> buf{};
        const int n = recv(conn, buf.data(), static_cast<int>(buf.size()), 0);
        if (n > 0) {
            send(conn, buf.data(), n, 0);
            ok.store(true, std::memory_order_release);
        }
        close_socket(conn);
        close_socket(listen_fd);
    });

    const uint16_t port = port_future.get();

    socket_t client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client == invalid_socket) {
        server.join();
        return 1;
    }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(port);

    if (connect(client, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) != 0) {
        close_socket(client);
        server.join();
        return 2;
    }

    const std::string payload = "jellybean-demo";
    send(client, payload.data(), static_cast<int>(payload.size()), 0);

    std::array<char, 128> reply{};
    const int rn = recv(client, reply.data(), static_cast<int>(reply.size()), 0);
    close_socket(client);
    server.join();

    if (rn <= 0) return 3;
    const std::string_view echoed(reply.data(), static_cast<size_t>(rn));
    return (echoed == payload && ok.load(std::memory_order_acquire)) ? 0 : 4;
}

std::vector<uint64_t> run_tcp_latency_samples(size_t samples) {
    std::promise<uint16_t> port_ready;
    auto port_future = port_ready.get_future();
    std::atomic<bool> running{true};

    std::thread server([&]() {
        socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == invalid_socket) return;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close_socket(listen_fd);
            return;
        }
        if (listen(listen_fd, 1) != 0) {
            close_socket(listen_fd);
            return;
        }
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
            close_socket(listen_fd);
            return;
        }
        port_ready.set_value(ntohs(bound.sin_port));

        socket_t conn = accept(listen_fd, nullptr, nullptr);
        if (conn == invalid_socket) {
            close_socket(listen_fd);
            return;
        }
        std::array<char, 32> buf{};
        while (running.load(std::memory_order_acquire)) {
            const int n = recv(conn, buf.data(), static_cast<int>(buf.size()), 0);
            if (n <= 0) break;
            send(conn, buf.data(), n, 0);
        }
        close_socket(conn);
        close_socket(listen_fd);
    });

    const uint16_t port = port_future.get();
    socket_t client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client == invalid_socket) {
        running.store(false, std::memory_order_release);
        server.join();
        return {};
    }
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(port);
    if (connect(client, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) != 0) {
        close_socket(client);
        running.store(false, std::memory_order_release);
        server.join();
        return {};
    }

    const std::string payload = "ping";
    std::array<char, 32> reply{};
    std::vector<uint64_t> lat;
    lat.reserve(samples);
    for (size_t i = 0; i < samples; ++i) {
        const auto t0 = steady_clock_t::now();
        send(client, payload.data(), static_cast<int>(payload.size()), 0);
        const int rn = recv(client, reply.data(), static_cast<int>(reply.size()), 0);
        const auto t1 = steady_clock_t::now();
        if (rn <= 0) break;
        lat.push_back(static_cast<uint64_t>(std::chrono::duration_cast<ns>(t1 - t0).count()));
    }

    close_socket(client);
    running.store(false, std::memory_order_release);
    server.join();
    return lat;
}

} // namespace

int main() {
    if (!init_sockets()) {
        std::cerr << "socket init failed\n";
        return 1;
    }

    // Actor mailbox throughput
    jellybean::actor::Mailbox mailbox;
    constexpr size_t mailbox_iters = 200000;
    const auto mb_t0 = steady_clock_t::now();
    for (size_t i = 0; i < mailbox_iters; ++i) {
        jellybean::actor::Message m;
        m.set<uint64_t>(static_cast<uint64_t>(i));
        if (!mailbox.try_push(std::move(m))) {
            std::cerr << "mailbox push failed\n";
            cleanup_sockets();
            return 2;
        }
        auto msg = mailbox.try_pop();
        if (!msg || msg->as<uint64_t>() != static_cast<uint64_t>(i)) {
            std::cerr << "mailbox pop mismatch\n";
            cleanup_sockets();
            return 3;
        }
    }
    const auto mb_t1 = steady_clock_t::now();
    const auto mb_ns = std::chrono::duration_cast<ns>(mb_t1 - mb_t0).count();
    const double mb_mops = (static_cast<double>(mailbox_iters) / static_cast<double>(mb_ns)) * 1e3;

    // Reactor timer latency
    jellybean::reactor::Reactor reactor(nullptr);
    std::vector<uint64_t> timer_lat_ns;
    constexpr int timer_samples = 200;
    timer_lat_ns.reserve(timer_samples);
    constexpr uint64_t delay_ns = 1 * 1000 * 1000;
    std::atomic<int> timer_fired{0};
    for (int i = 0; i < timer_samples; ++i) {
        const auto scheduled_at = steady_clock_t::now();
        reactor.add_timer(delay_ns, [scheduled_at, &timer_lat_ns, &timer_fired, &reactor]() {
            const auto now = steady_clock_t::now();
            const auto elapsed = std::chrono::duration_cast<ns>(now - scheduled_at).count();
            const auto jitter = elapsed - static_cast<int64_t>(delay_ns);
            timer_lat_ns.push_back(static_cast<uint64_t>(jitter > 0 ? jitter : 0));
            const int fired = timer_fired.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (fired == timer_samples) {
                reactor.stop();
            }
        });
    }
    reactor.run();
    if (timer_lat_ns.empty()) {
        std::cerr << "reactor timer failed\n";
        cleanup_sockets();
        return 4;
    }

    // Real TCP loopback echo smoke
    const int tcp_status = run_tcp_echo_roundtrip();
    if (tcp_status != 0) {
        std::cerr << "tcp loopback failed: " << tcp_status << "\n";
        cleanup_sockets();
        return 5;
    }

    // TCP loopback latency samples
    auto tcp_lat_ns = run_tcp_latency_samples(300);
    if (tcp_lat_ns.empty()) {
        std::cerr << "tcp latency sampling failed\n";
        cleanup_sockets();
        return 6;
    }

    const uint64_t timer_p50 = percentile_ns(timer_lat_ns, 0.50);
    const uint64_t timer_p99 = percentile_ns(timer_lat_ns, 0.99);
    const uint64_t tcp_p50 = percentile_ns(tcp_lat_ns, 0.50);
    const uint64_t tcp_p99 = percentile_ns(tcp_lat_ns, 0.99);
    const uint64_t tcp_avg = static_cast<uint64_t>(
        std::accumulate(tcp_lat_ns.begin(), tcp_lat_ns.end(), 0ULL) / tcp_lat_ns.size());

    std::cout << "demo ok\n";
    std::cout << "mailbox throughput: " << mb_mops << " Mops/s\n";
    std::cout << "timer lateness ns: p50=" << timer_p50 << " p99=" << timer_p99 << "\n";
    std::cout << "tcp rtt ns: avg=" << tcp_avg << " p50=" << tcp_p50 << " p99=" << tcp_p99 << "\n";
    cleanup_sockets();
    return 0;
}
