#include "include/file.h"
#include "log.h"

#include <liburing.h>

namespace LSMKV {

  UringWritableFile::UringWritableFile(const std::string &fname, int fd)
          : fd_(fd), offset_(0), filename_(fname) {
      io_uring_queue_init(16, &ring_, 0);
      chunks_.resize(2);

      iovec iov[2];
      for (int i = 0; i < 2; i++) {
          iov[i].iov_base = chunks_[i].data;
          iov[i].iov_len = kChunkSize;
      }

      io_uring_register_buffers(&ring_, iov, chunks_.size());
  }

  UringWritableFile::~UringWritableFile() {
      Close();
      io_uring_queue_exit(&ring_);
  }

  Status UringWritableFile::Append(const Slice &data) {
      auto write_data = data.data();
      uint32_t write_size = data.size();

      const auto copy_size = std::min(write_size, kChunkSize - pos_);

      utils::m_memcpy(chunks_[chunk_pos_ & 1].data + pos_, write_data, copy_size);

      write_data += copy_size;
      write_size -= copy_size;
      pos_ += copy_size;


      if (write_size == 0) {
          return Status::OK();
      }

      auto status = Flush();

      if (!status.ok()) {
          return status;
      }

      // Small writes go to buffer, large writes are written directly.
      if (write_size < kWritableFileBufferSize) {
          return Append(Slice(write_data, write_size));
      }

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
      io_uring_prep_write(sqe, fd_, write_data, write_size, offset_);
      offset_ += copy_size;
      sqe->user_data = kLargeValueTag;

      auto ret = io_uring_submit(&ring_);

      if (ret < 0) {
          log::error("io_uring_submit failed: {}", std::strerror(-ret));
          return Status::IOError("io_uring_submit failed" + std::string{std::strerror(-ret)});
      }
      return ProcessCompletions(kLargeValueTag);
  }

  Status UringWritableFile::Flush() {
      Status s = Status::OK();

      if (SubmitIO().ok() && !ready_chunks_[chunk_pos_ & 1]) {
          s = ProcessCompletions(MaskChunk(chunk_pos_));
      }

      return s;
  }

  Status UringWritableFile::Sync() {
      Flush();

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);

      io_uring_prep_fsync(sqe, fd_, IORING_FSYNC_DATASYNC);
      sqe->user_data = kSyncTag;

      auto ret = io_uring_submit(&ring_);
      if (ret < 0) {
          log::error("io_uring_submit failed: {}", std::strerror(-ret));
          return Status::IOError("io_uring_submit failed" + std::string{std::strerror(-ret)});
      }


      return ProcessCompletions(kSyncTag);
  }

  Status UringWritableFile::Close() {
      if (fd_ < 0) return Status::OK();
      auto chunk = chunk_pos_;
      if (!ready_chunks_[(chunk + 1) & 1]) {
          ProcessCompletions(MaskChunk(chunk + 1));
      }

      if (SubmitIO().ok()) {
          ProcessCompletions(MaskChunk(chunk));
      }

      ::close(fd_);
      fd_ = -1;
      return Status::OK();
  }

  Status UringWritableFile::SubmitIO() {
      if (pos_ == 0) {
          return Status::NotFound("No data to write" + line_info());
      }

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
      ready_chunks_[chunk_pos_ & 1] = false;
      sqe->user_data = MaskChunk(chunk_pos_);

      io_uring_prep_write(sqe, fd_, chunks_[(chunk_pos_++) & 1].data, pos_,
                          offset_);
      offset_ += pos_;

      pos_ = 0;

      auto ret = io_uring_submit(&ring_);

      if (ret < 0) {
          log::error("io_uring_submit failed: {}", std::strerror(-ret));
          return Status::IOError("io_uring_submit failed" + std::string{std::strerror(-ret)});
      }
      return Status::OK();
  }

  Status UringWritableFile::ProcessCompletions(write_tag tag) {
      Status final_status = Status::OK();
      unsigned head;
      struct io_uring_cqe *cqe;

      while (true) {
          int ret = io_uring_peek_cqe(&ring_, &cqe);
          if (ret == -EAGAIN) {
              do {
                  ret = io_uring_wait_cqe(&ring_, &cqe);
              } while (ret == -EINTR);
              if (ret < 0) {
                  return Status::IOError(std::string{"io_uring_wait_cqe failed:"} + std::strerror(-ret));
              }
          } else if (ret < 0) {
              return Status::IOError("io_uring_peek_cqe failed" +
                                     std::string{std::strerror(-ret)});
          }

          unsigned count = 0;
          bool target_found = false;
          io_uring_for_each_cqe(&ring_, head, cqe) {
              count++;

              if (cqe->res < 0 && final_status.ok()) {
                  final_status = Status::IOError("IO operation failed" + std::string
                          (std::strerror(-cqe->res)));
              }

              if (cqe->user_data == kChunk1Tag) {
                  ready_chunks_[0] = true;
              } else if (cqe->user_data == kChunk2Tag) {
                  ready_chunks_[1] = true;
              }

              if (cqe->user_data == tag) {
                  target_found = true;
              }
          }

          io_uring_cq_advance(&ring_, count);

          if (target_found || !final_status.ok()) {
              break;
          }
      }

      if (!final_status.ok()) {
          return final_status;
      }

      return Status::OK();
  }

} // namespace LSMKV