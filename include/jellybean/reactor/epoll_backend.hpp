#pragma once
#include "jellybean/reactor/reactor.hpp"

namespace jellybean::reactor {

class EpollBackend final : public EventBackend {
   public:
    EpollBackend();
    virtual ~EpollBackend() noexcept override;
    virtual auto poll(int timeout_ms) noexcept -> int override;
    auto epoll_fd() const noexcept -> int;

   private:
    int epoll_fd_;
};

}  // namespace jellybean::reactor
