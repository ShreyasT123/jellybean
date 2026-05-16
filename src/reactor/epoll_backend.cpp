#include "jellybean/reactor/epoll_backend.hpp"

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#include <stdexcept>

namespace jellybean::reactor {

EpollBackend::EpollBackend() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll fd");
    }
}

EpollBackend::~EpollBackend() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

int EpollBackend::poll(int timeout_ms) {
    struct epoll_event events[128];
    int nfds = epoll_wait(epoll_fd_, events, 128, timeout_ms);
    
    for (int n = 0; n < nfds; ++n) {
        // Process events...
        // This would involve calling callbacks for registered fds
    }
    
    return nfds;
}

} // namespace jellybean::reactor
#endif
