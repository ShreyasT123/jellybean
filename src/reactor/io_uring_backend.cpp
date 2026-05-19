#include "jellybean/reactor/io_uring_backend.hpp"
#include <liburing.h>
#include <cstdio>
#include <cstdlib>

namespace jellybean::reactor {

IoUringBackend::IoUringBackend() {
    if (io_uring_queue_init(4096, &ring_, 0) < 0) {
        fprintf(stderr, "Fatal: Failed to initialize io_uring\n");
        std::abort();
    }
}

IoUringBackend::~IoUringBackend() {
    io_uring_queue_exit(&ring_);
}

int IoUringBackend::poll(int timeout_ms) {
    struct io_uring_cqe* cqe;
    struct __kernel_timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;

    int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, timeout_ms > 0 ? &ts : nullptr);
    if (ret < 0) return ret;

    // Process completions...
    // This would ideally call callbacks registered in submit_*
    
    io_uring_cqe_seen(&ring_, cqe);
    return 1;
}

void IoUringBackend::submit_read(int fd, void* buf, size_t len, off_t offset, void* user_data) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_read(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data(sqe, user_data);
    io_uring_submit(&ring_);
}

} // namespace jellybean::reactor
