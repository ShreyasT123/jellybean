#pragma once
#include "jellybean/reactor/reactor.hpp"
#include <liburing.h>

namespace jellybean::reactor {

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

} // namespace jellybean::reactor
