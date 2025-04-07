#pragma once

#include <coroutine>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <liburing.h>
#include "utils/status.h"

namespace LSMKV {
  class UringExecutor {
  public:
      UringExecutor(size_t queue_size = 12) {
          if (io_uring_queue_init(queue_size, &ring_, 0) != 0) {
              throw std::runtime_error("io_uring initialization failed");
          }
      }

      ~UringExecutor() {
          io_uring_queue_exit(&ring_);
      }

      template<typename ADDRESS>
      Status async_read(int fd, char *buf, size_t size, uint64_t offset,
                        ADDRESS &&data) {
          io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
          if (!sqe) {
              return Status::IOError("SQ ring full");
          }

          io_uring_prep_read(sqe, fd, buf, size, offset);
          io_uring_sqe_set_data(sqe, data);

          if (int ret = io_uring_submit(&ring_); ret < 0) {
              return Status::IOError("Failed to submit read request");
          }

          return Status::OK();
      }

      template<typename Func>
      void process_completions_w_call_back(Func &&f) {
          io_uring_cqe *cqe;
          io_uring_wait_cqe(&ring_, &cqe);
          unsigned head;
          unsigned count = 0;

          io_uring_for_each_cqe(&ring_, head, cqe) {
              ++count;

              if (cqe->res >= 0) {
                  f(io_uring_cqe_get_data(cqe));
              } else {
                  std::cerr << "Read error: " << strerror(-cqe->res) << "\n";
              }
          }
          io_uring_cq_advance(&ring_, count);
      }

      void process_completions() {
          io_uring_cqe *cqe;
          io_uring_wait_cqe(&ring_, &cqe);
          unsigned head;
          unsigned count = 0;

          io_uring_for_each_cqe(&ring_, head, cqe) {
              ++count;

              if (cqe->res < 0) {
                  std::cerr << "Read error: " << strerror(-cqe->res) << "\n";
              }
          }
          io_uring_cq_advance(&ring_, count);
      }

  private:
      io_uring ring_;
  };


} // namespace LSMKV