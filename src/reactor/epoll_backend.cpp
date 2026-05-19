#include "jellybean/reactor/epoll_backend.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <coroutine>
#include <cstdio>
#include <cstdlib>

namespace jellybean::reactor {

EpollBackend::EpollBackend() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        fprintf(stderr, "Fatal: Failed to create epoll fd\n");
        std::abort();
    }
}

EpollBackend::~EpollBackend() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

int EpollBackend::poll(int timeout_ms) noexcept {
    struct epoll_event events[128];
    int nfds = epoll_wait(epoll_fd_, events, 128, timeout_ms);
    if (nfds < 0) {
        if (errno != EINTR) {
            perror("epoll_wait");
        }
    }

    for (int n = 0; n < nfds; ++n) {
        // Each event's data.ptr carries the raw address of a suspended
        // coroutine_handle<>. Reconstruct and resume it directly from
        // the epoll thread — this is the core of the async dispatch loop.
        if (events[n].data.ptr) {
            auto h = std::coroutine_handle<>::from_address(events[n].data.ptr);
            if (h && !h.done()) {
                h.resume();
            }
        }
    }

    return nfds;
}

int EpollBackend::epoll_fd() const noexcept {
    return epoll_fd_;
}

}  // namespace jellybean::reactor
