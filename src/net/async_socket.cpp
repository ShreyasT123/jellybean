#include "jellybean/net/async_socket.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

namespace jellybean::net {

void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        std::abort();
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::abort();
    }
}

void set_tcp_nodelay(int fd) {
    int yes = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        // Log or handle
    }
}

void AsyncReadAwaitable::await_suspend(std::coroutine_handle<> h) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
    ev.data.ptr = h.address();
    if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) == -1 && errno == EEXIST) {
        ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock_fd, &ev);
    }
}

ssize_t AsyncReadAwaitable::await_resume() noexcept {
    ssize_t total = 0;
    auto* dst = static_cast<char*>(buf);
    while (total < static_cast<ssize_t>(len)) {
        const ssize_t n = ::recv(sock_fd, dst + total, len - static_cast<size_t>(total), MSG_DONTWAIT);
        if (n > 0) {
            total += n;
            continue;
        }
        if (n == 0) {
            result = total;
            return result;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        result = -1;
        return result;
    }
    result = total;
    return result;
}

void AsyncWriteAwaitable::await_suspend(std::coroutine_handle<> h) {
    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLONESHOT | EPOLLET;
    ev.data.ptr = h.address();
    if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) == -1 && errno == EEXIST) {
        ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock_fd, &ev);
    }
}

ssize_t AsyncWriteAwaitable::await_resume() noexcept {
    return ::send(sock_fd, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL);
}

AsyncSocket::AsyncSocket(int fd, int epoll_fd)
    : fd_(fd), epoll_fd_(epoll_fd) {
    set_nonblocking(fd_);
    set_tcp_nodelay(fd_);
}

AsyncSocket::~AsyncSocket() {
    if (fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd_, nullptr);
        ::close(fd_);
    }
}

} // namespace jellybean::net
