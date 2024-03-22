#pragma once

#include "liburing.h"

class IOUring {
public:
    explicit IOUring(size_t queue_size) {
        if (auto s = io_uring_queue_init(queue_size, &ring_, 0); s < 0) {
            // TODO: error handling
//            throw std::runtime_error("error initializing io_uring: " + std::to_string(s));
        }
    }

    IOUring(const IOUring &) = delete;

    IOUring &operator=(const IOUring &) = delete;

    IOUring(IOUring &&) = delete;

    IOUring &operator=(IOUring &&) = delete;

    ~IOUring() { io_uring_queue_exit(&ring_); }

    struct io_uring *get() {
        return &ring_;
    }

private:
    struct io_uring ring_;
};