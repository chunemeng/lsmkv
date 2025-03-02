#ifndef VERSION_H
#define VERSION_H

#include "utils/bloomfilter.h"
#include "utils/coding.h"
#include "file.h"
#include "utils/filename.h"
#include "utils/utils.h"
#include "utils/executor.h"
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace LSMKV {
  struct Version {
      explicit Version(const std::string &dbname) {
          auto vlog_file = dbname + "/redo.log";
          NewWritableNoBufFile(vlog_file, &redo_vlog_file);
      }

      ~Version() = default;

      void reset() {
          fileno_ = 0;
          last_vlog_file_no_ = 0;
          max_level = 7;
      }

      uint64_t NewVLogFileNumber() {
          return last_vlog_file_no_.fetch_add(1);
      }

      uint64_t NewSSTFileNumber() {
          return fileno_;
      }

      void ReuseSSTFileNumber(uint64_t file_no) {
          if (fileno_ + 1 == file_no) {
              fileno_ = file_no;
          }
      }

      void ReuseVLogFileNumber(uint64_t file_no) {
          if (file_no + 1 == last_vlog_file_no_) {
              last_vlog_file_no_ = file_no;
          }
      }

      void SetSSTFileNumber(uint64_t file_no) {
          fileno_ = file_no;
      }

      void SetVLogFileNumber(uint64_t file_no) {
          last_vlog_file_no_ = file_no;
      }

      void SetLastSequence(uint64_t seq) {
          assert(seq > last_sequence_);
          last_sequence_.store(seq, std::memory_order_release);
      }

      uint64_t LastSequence() const {
          return last_sequence_.load(std::memory_order_acquire);
      }

      uint64_t LastLivingSequence() const {
          return last_live_sequence_.load(std::memory_order_acquire);
      }

      void SetLastLivingSequence(uint64_t seq) {
          last_live_sequence_.store(seq, std::memory_order_release);
      }

      uint64_t fileno_ = 0;

      std::unique_ptr<WritableFile> redo_vlog_file;

      uint64_t max_level = 7;

      std::atomic<uint64_t> last_sequence_ = 0;

      std::atomic<uint64_t> last_live_sequence_ = 0;

      std::atomic<uint64_t> last_vlog_file_no_ = 0;

      std::shared_ptr<Executor> executor_ = std::make_shared<Executor>(2);
  };
}

#endif //VERSION_H
