#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/types.h>

namespace jellybean::net {

// Makes a file descriptor non-blocking. Required for epoll-driven async I/O.
void set_nonblocking(int fd);

// Disable Nagle's algorithm for lower latency on small packets.
void set_tcp_nodelay(int fd);

/**
 * @brief Awaitable: registers fd with epoll EPOLLIN, suspends coroutine until
 * the kernel signals readability, then resumes.
 */
struct AsyncReadAwaitable {
    int epoll_fd;
    int sock_fd;
    void* buf;
    size_t len;
    ssize_t result{0};

    auto await_ready() const noexcept -> bool {
        return false;
    }
    void await_suspend(std::coroutine_handle<> h);
    auto await_resume() noexcept -> ssize_t;
};

/**
 * @brief Awaitable: registers fd with epoll EPOLLOUT, suspends coroutine until
 * the kernel signals writability, then resumes.
 */
struct AsyncWriteAwaitable {
    int epoll_fd;
    int sock_fd;
    const void* buf;
    size_t len;

    auto await_ready() const noexcept -> bool {
        return false;
    }
    void await_suspend(std::coroutine_handle<> h);
    auto await_resume() noexcept -> ssize_t;
};

/**
 * @brief High-level async socket wrapping a raw fd.
 *
 * Methods return awaitables that can be co_awaited from inside a
 * jellybean::scheduler::Task coroutine running on a Reactor thread.
 */
class AsyncSocket {
   public:
    explicit AsyncSocket(int fd, int epoll_fd);
    ~AsyncSocket();

    AsyncSocket(const AsyncSocket&) = delete;
    auto operator=(const AsyncSocket&) -> AsyncSocket& = delete;

    AsyncSocket(AsyncSocket&& o) noexcept : fd_(o.fd_), epoll_fd_(o.epoll_fd_) {
        o.fd_ = -1;
    }

    // co_await socket.read(buf, len) → ssize_t bytes read
    auto read(void* buf, size_t len) noexcept -> AsyncReadAwaitable {
        return {epoll_fd_, fd_, buf, len};
    }

    // co_await socket.write(buf, len) → ssize_t bytes written
    auto write(const void* buf, size_t len) noexcept -> AsyncWriteAwaitable {
        return {epoll_fd_, fd_, buf, len};
    }

    auto fd() const noexcept -> int {
        return fd_;
    }

   private:
    int fd_;
    int epoll_fd_;
};

}  // namespace jellybean::net
