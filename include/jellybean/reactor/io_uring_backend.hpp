#pragma once
#include "jellybean/reactor/reactor.hpp"

#if defined(__linux__)
#include <liburing.h>
#endif

namespace jellybean::reactor {

#if defined(__linux__)
class IoUringBackend : public EventBackend {
public:
    IoUringBackend();
    ~IoUringBackend() override;
    int poll(int timeout_ms) override;

    void submit_read(int fd, void* buf, size_t len, off_t offset, void* user_data);
    // ... other I/O ops

private:
    struct io_uring ring_;
};
#endif

} // namespace jellybean::reactor
