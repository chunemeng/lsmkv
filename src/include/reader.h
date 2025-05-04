#pragma once

#include "block_format.h"
#include "utils/status.h"
#include "io_uring_reader.h"
#include "utils/arena.h"

namespace LSMKV {
  namespace detail {
    struct Task {
        struct promise_type;
        using Handle = std::coroutine_handle<promise_type>;

        struct ReturnType {
            Status s;
            VLogEntryInfo *vlog_info;
        };

        struct promise_type {
            VLogEntryInfo vlog_info;
            Status s;

            Task get_return_object() {
                return Task{Handle::from_promise(*this)};
            }

            static std::suspend_never initial_suspend() { return {}; }

            std::suspend_always final_suspend() noexcept { return {}; }

            void return_value(ReturnType &&ret) {
                this->s = std::move(ret.s);
                if (s.ok()) {
                    vlog_info = std::move(*ret.vlog_info);
                }
            }

            static void unhandled_exception() { std::terminate(); }

        };

        explicit Task(Handle h) : h_(h) {}

        ~Task() = default;

        Handle h_;
    };

    struct VLogReadTask {
        struct promise_type;
        using Handle = std::coroutine_handle<promise_type>;

        struct promise_type {
            Status s;

            VLogReadTask get_return_object() {
                return VLogReadTask{Handle::from_promise(*this)};
            }

            static std::suspend_never initial_suspend() { return {}; }

            std::suspend_always final_suspend() noexcept { return {}; }

            void return_value(Status s) {
                this->s = std::move(s);
            }

            static void unhandled_exception() { std::terminate(); }

        };

        explicit VLogReadTask(Handle h) : h_(h) {}

        ~VLogReadTask() = default;

        Handle h_;
    };


    struct CoroutineHandleAwaiter {
        std::coroutine_handle<> captured_handle;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            captured_handle = h;
            h.resume();
        }

        std::coroutine_handle<> await_resume() const noexcept {
            return captured_handle;
        }
    };

  }

  class SSTReader {
  public:
      SSTReader(const std::string &db_name) : db_name_(db_name) {
      }

  private:
      static Status FilterInIndex(Slice input, const Slice &internal_key, BlockEntryInfo *block_entry_info) {
          Comparator cmp = InternalKeyComparator();

          while (!input.empty()) {
              Slice block_entry;
              auto status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  return status;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  return Status::Corruption("bad block entry");
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              if (cmp.compare(internal_key, Slice(block_entry.data() + 8, key_size)) <= 0) {
                  block_entry.remove_prefix(key_size + 8);
                  BlockEntryInfo b_info{};
                  status = b_info.Decode(block_entry);
                  if (!status.ok()) {
                      return status;
                  }
                  *block_entry_info = b_info;
                  return Status::OK();
              }
          }

          return Status::NotFound("not found key in FilterInIndex " + line_info());
      }

      static Status FindByModel(Slice input, const Slice &internal_key, std::pair<int64_t, int64_t> *location);

      static Status FindInBlock(Slice input, const Slice &internal_key, VLogEntryInfo *vlog_info) {
          Comparator user_cmp = StrComparator();
          Status status = Status::OK();

          while (!input.empty()) {
              Slice block_entry;
              status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  return status;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  return Status::Corruption("bad block entry");
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              auto block_internal_key = Slice(block_entry.data() + 8, key_size);

              auto ret = user_cmp.compare(ExtractUserKey(internal_key), ExtractUserKey(block_internal_key));

              if (ret < 0) {
                  return Status::NotFound("not found key in FindInBlock " + line_info());
              }

              if (ret == 0) {
                  auto seq = ExtractSequenceNumber(block_internal_key);
                  auto query_seq = ExtractSequenceNumber(internal_key);
                  auto value_type = ExtractValueType(block_internal_key);

                  if (seq <= query_seq) {
                      block_entry.remove_prefix(key_size + 8);
                      if (value_type == kTypeValue) {
                          status = vlog_info->Decode(block_entry);
                      }

                      return status;
                  }

                  return Status::NotFound("not found key in FindInBlock " + line_info());
              }
          }
          return Status::NotFound("not found key in FindInBlock " + line_info());
      }

      Status ReadOne(const SSTFileMeta *meta, Slice internal_key, VLogEntryInfo *vlog_info);

      detail::Task Async_ReadOne(UringExecutor &executor, const SSTFileMeta *meta, const Slice &internal_key) {
          // sst k|v_ptr k|v_ptr
          // vlog k|v k|v
          auto file_name = SSTFileName(db_name_, meta->file_number_);

          int fd = open(file_name.c_str(), O_RDONLY | kOpenBaseFlags);

          struct Guard {
              uint32_t &active_tasks_;
              int fd_;

              Guard(uint32_t &active_tasks, int fd) : active_tasks_(active_tasks), fd_(fd) { ++active_tasks_; }

              ~Guard() {
                  --active_tasks_;
                  if (fd_ > 0) {
                      close(fd_);
                  }

              }
          } guard(active_tasks, fd);

          if (fd < 0) {
              guard.~Guard();
              co_return {Status::IOError("open file failed"), nullptr};
          }

          Footer footer{};

          Slice input;
          std::string buffer;
          buffer.resize(Footer::kEncodedLength);
          input = buffer;


          auto h = co_await detail::CoroutineHandleAwaiter{};

          auto status = executor.async_read(fd, buffer.data(), Footer::kEncodedLength,
                                            meta->file_size_ - Footer::kEncodedLength,
                                            h.address());
          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          co_await std::suspend_always{};

          status = footer.Decode(input);

          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          const auto &metaindex_handle = footer.metaindex_handle();
          const auto &index_handle = footer.index_handle();

          buffer.resize(metaindex_handle.size_);
          status = executor.async_read(fd, buffer.data(), metaindex_handle.size_, metaindex_handle.offset_,
                                       h.address());

          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          co_await std::suspend_always{};

          input = buffer;

          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          if (!KeyMayMatch(ExtractUserKey(internal_key), input)) {
              guard.~Guard();
              co_return {Status::NotFound("Filtered by bloom in " + line_info()), nullptr};
          }

          buffer.resize(index_handle.size_);

          status = executor.async_read(fd, buffer.data(), index_handle.size_, index_handle.offset_, h.address());
          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          co_await std::suspend_always{};
          input = buffer;

          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          BlockEntryInfo b_info{};

          status = FilterInIndex(input, internal_key, &b_info);

          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          buffer.resize(b_info.size_);

          status = executor.async_read(fd, buffer.data(), b_info.size_, b_info.offset_, h.address());
          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          co_await std::suspend_always{};
          input = buffer;
          VLogEntryInfo info{};
          status = FindInBlock(input, internal_key, &info);
          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status), nullptr};
          }

          guard.~Guard();
          co_return {std::move(status), &info};
      }

      Status
      ReadAsyncBatch(const std::vector<const SSTFileMeta *> &metas, Slice internal_key, VLogEntryInfo *vlog_info) {
          UringExecutor executor(metas.size());
          std::vector<detail::Task> tasks;
          active_tasks += metas.size();

          log::debug("meta size: {}", metas.size());
          for (const auto &meta: metas) {
              tasks.emplace_back(Async_ReadOne(executor, meta, internal_key));
          }

          auto fun = +[](void *data) {
              auto handle = std::coroutine_handle<detail::Task::promise_type>::from_address(data);
              handle.resume();
          };

          while (active_tasks > 0) {
              executor.process_completions_w_call_back(fun);
          }

          for (auto &task: tasks) {
              assert(task.h_.done());

              auto promise = task.h_.promise();

              if (promise.s.ok()) {
                  *vlog_info = promise.vlog_info;
                  return Status::OK();
              } else if (!promise.s.IsNotFound()) {
                  return promise.s;
              }
          }

          return Status::NotFound(line_info());
      }


  public:
      Status ReadBatch(const std::vector<const SSTFileMeta *> &metas, Slice internal_key, VLogEntryInfo *vlog_info) {
          if (metas.size() == 1) {
              return ReadOne(*metas.begin(), internal_key, vlog_info);
          }


//          for (const auto &meta: metas) {
//              auto status = ReadOne(meta, internal_key, vlog_info);
//              if (status.ok() || !status.IsNotFound()) {
//                  return status;
//              }
//          }
//          return Status::NotFound();

          return ReadAsyncBatch(metas, internal_key, vlog_info);
      }

  private:
      uint32_t active_tasks = 0;

      const std::string db_name_;
  };

  class CoroVLogReader {
  private:
      std::shared_ptr<Version> version_{};

      const std::string db_name_;
      UringExecutor executor_{20};

      uint32_t active_tasks = 0;
  public:
      explicit CoroVLogReader(const std::string &db_name, std::shared_ptr<Version> version = nullptr) : db_name_(
              db_name), version_(std::move(version)) {
      }

      detail::VLogReadTask Async_ReadOne(VLogEntryInfo info, Arena *arena, std::string *value) {
          auto file_name = VLogFileName(db_name_, info.file_no_);
          int fd = open(file_name.c_str(), O_RDONLY | kOpenBaseFlags);

          struct Guard {
              uint32_t &active_tasks_;
              int fd_;

              Guard(uint32_t &active_tasks, int fd) : active_tasks_(active_tasks), fd_(fd) { ++active_tasks_; }

              ~Guard() {
                  --active_tasks_;
                  if (fd_ > 0) {
                      close(fd_);
                  }
              }
          } guard(active_tasks, fd);

          auto *buf = arena->allocate(info.length_ + Option::kBlockTrailerSize);

          auto status = Status::OK();
          if (!status.ok()) {
              guard.~Guard();
              co_return {std::move(status)};
          }

          auto h = co_await detail::CoroutineHandleAwaiter{};

          status = executor_
                  .async_read_wno_submit(fd, buf, info.length_ + Option::kBlockTrailerSize, info.offset_, h.address());

          co_await std::suspend_always{};

          if (!status.ok()) {
              log::debug("async read failed: {}", status.ToString());
              guard.~Guard();

              co_return {std::move(status)};
          }

          uint32_t key_size = DecodeFixed32(buf);
          uint32_t value_size = DecodeFixed32(buf + 4);

          assert(key_size + value_size + 8 == info.length_);

          auto crc32 = DecodeFixed32(buf + 8 + key_size + value_size);
          auto crc = crc32c::Crc32c(buf, key_size + value_size + 8);

          if (crc32 != crc) {
              log::debug("crc32: {}, crc: {}", crc32, crc);
              guard.~Guard();
              co_return {Status::Corruption("bad crc")};
          }

          if (version_ != nullptr) {
              auto live_seq = version_->LastLivingSequence();
              auto seq = ExtractSequenceNumber(Slice(buf + 8, key_size));

              if (seq < live_seq) {
                  log::debug("seq: {}, live_seq: {}", seq, live_seq);
                  guard.~Guard();
                  co_return {Status::Expired()};
              }
          }

          *value = {buf + key_size + 8, value_size};

          guard.~Guard();
          co_return {Status::OK()};
      }


      Status ReadList(std::list<std::pair<std::string, std::string>> &list) {
          std::vector<detail::VLogReadTask> tasks;
          active_tasks += list.size();

          log::info("siaiz: {}", active_tasks);


          auto fun = +[](void *data) {
              auto handle = std::coroutine_handle<detail::Task::promise_type>::from_address(data);
              handle.resume();
          };

          Arena arena{};

          Status status = Status::OK();
          tasks.reserve(list.size());

          for (auto it = list.begin(); it != list.end();) {
              while (executor_.count() >= executor_.size()) {
                  executor_.submit();
                  executor_.process_completions_w_call_back(fun);
              }

              if (it->second.empty()) {
                  it = list.erase(it);
                  active_tasks--;
                  continue;
              }

              LSMKV::VLogEntryInfo info{};
              status = info.Decode(it->second);

              if (!status.ok()) [[unlikely]] {
                  active_tasks--;
              }

              tasks.emplace_back(Async_ReadOne(info, &arena, &it->second));
              it++;
          }
          executor_.submit();

          while (active_tasks > 0) {
              executor_.process_completions_w_call_back(fun);
          }

          for (auto &task: tasks) {
              assert(task.h_.done());

              auto promise = task.h_.promise();

              if (!promise.s.IsNotFound()) {
                  return promise.s;
              }
          }
          return Status::OK();
      }
  };

  class VLogReader {
  private:
      std::shared_ptr<Version> version_{};

      std::mutex file_mutex_;
      const std::string db_name_;
      Lru_Cache<uint32_t, std::shared_ptr<RandomReadableFile>> lru_{5};

      Status InitFile(uint32_t file_no, std::shared_ptr<RandomReadableFile> &current_file) {
          std::unique_lock<std::mutex> lock(file_mutex_);

          auto file_p = lru_.Get(file_no);

          if (file_p != nullptr) {
              current_file = *file_p;
              return Status::OK();
          }
          std::string fname = VLogFileName(db_name_, file_no);
          std::unique_ptr<RandomReadableFile> file;
          auto status = NewRandomReadableFile(fname, &file);
          if (!status.ok()) {
              return status;
          }

          current_file = std::move(file);

          lru_.Put(file_no, current_file);

          return Status::OK();
      }

  public:
      explicit VLogReader(const std::string &db_name, std::shared_ptr<Version> version = nullptr) : db_name_(db_name),
                                                                                                    version_(std::move(
                                                                                                            version)) {
      }


      Status Read(const VLogEntryInfo &info, std::string *value) {
          std::vector<char> tmp(info.length_ + Option::kBlockTrailerSize);
          auto *buf = tmp.data();

          std::shared_ptr<RandomReadableFile> current_file;

          auto status = InitFile(info.file_no_, current_file);
          if (!status.ok()) {
              return status;
          }

          Slice result_slice;
          status = current_file->Read(info.offset_, info.length_ + Option::kBlockTrailerSize, &result_slice, buf);
          if (!status.ok()) {
              return status;
          }

          if (result_slice.size() != info.length_ + Option::kBlockTrailerSize) {
              return Status::Corruption("bad vlog entry");
          }

          uint32_t key_size = DecodeFixed32(buf);
          uint32_t value_size = DecodeFixed32(buf + 4);

          assert(key_size + value_size + 8 == info.length_);

          auto crc32 = DecodeFixed32(buf + 8 + key_size + value_size);
          auto crc = crc32c::Crc32c(buf, key_size + value_size + 8);

          if (crc32 != crc) {
              return Status::Corruption("bad crc");
          }

          if (version_ != nullptr) {
              auto live_seq = version_->LastLivingSequence();
              auto seq = ExtractSequenceNumber(Slice(buf + 8, key_size));

              if (seq < live_seq) {
                  return Status::Expired();
              }
          }

          *value = {buf + key_size + 8, value_size};

          return Status::OK();
      }
  };

} // namespace LSMKV