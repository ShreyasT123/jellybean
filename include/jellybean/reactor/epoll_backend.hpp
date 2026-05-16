#pragma once
#include "jellybean/reactor/reactor.hpp"

namespace jellybean::reactor {

#if defined(__linux__)
class EpollBackend : public EventBackend {
public:
    EpollBackend();
    ~EpollBackend() override;
    int poll(int timeout_ms) override;

private:
    int epoll_fd_;
};
#endif

} // namespace jellybean::reactor
