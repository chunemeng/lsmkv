#ifndef VERSION_H
#define VERSION_H

#include "utils/bloomfilter.h"
#include "utils/coding.h"
#include "utils/file.h"
#include "utils/filename.h"
#include "utils/utils.h"
#include "queue.hpp"
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
//  class WriteScheduler {
//  public:
//      struct Request {
//          bool isWrite_;
//          std::string file_name;
//          WriteSlice slice;
//          std::promise<void> callback_;
//      };
//
//      explicit WriteScheduler(int size) {
//          for (int i = 0; i < size; i++) {
//              background_thread_.emplace_back([&] { StartSchedule(); });
//          }
//      }
//
//      ~WriteScheduler() {
//          for (int i = 0; i < background_thread_.size(); i++) {
//              request_queue_.push(std::nullopt);
//          }
//          background_thread_.clear();
//      }
//
//      void Schedule(Request r) {
//          request_queue_.push(std::move(r));
//      }
//
//      void StartSchedule() {
//          std::optional<Request> req;
//          WritableNoBufFile *file;
//          while ((req = std::move(request_queue_.pop())) != std::nullopt) {
//              if (req->isWrite_) {
//                  WriteSlice s = req->slice;
//                  char *tmp = s.data();
//                  memset(tmp + 32, 0, bloom_size);
//                  CreateFilter(tmp + bloom_size + 32, DecodeFixed64(tmp + 8), 20, tmp + 32);
//                  NewWritableNoBufFile(req->file_name, &file);
//                  file->WriteUnbuffered(tmp, s.size());
//                  delete file;
//                  req->callback_.set_value();
//              } else {
//                  utils::rmfile(req->file_name);
//                  req->callback_.set_value();
//              }
//          }
//      };
//
//  private:
//      Queue<std::optional<Request>> request_queue_;
//      /** The background thread responsible for issuing scheduled requests to the disk manager. */
//      std::vector<std::jthread> background_thread_;
//  };

  struct Request {
      bool isWrite_;
      std::string file_name;
      WriteSlice slice;
  };

  static inline void WriteSchedule(const Request &req) {
      WritableNoBufFile *file;

      if (req.isWrite_) {
          WriteSlice s = req.slice;
          char *tmp = s.data();
          memset(tmp + 32, 0, bloom_size);
          CreateFilter(tmp + bloom_size + 32, DecodeFixed64(tmp + 8), 20, tmp + 32);
          NewWritableNoBufFile(req.file_name, &file);
          file->WriteUnbuffered(tmp, s.size());
          delete file;
      } else {
          utils::rmfile(req.file_name);
      }

  };


  struct Version {
      explicit Version(const std::string &dbname) : filename(VersionFileName(dbname)) {
          // read from current file
          auto vlog_files = std::move(VLogFileName(dbname, 0));
          if (FileExists(vlog_files)) {
              head = GetFileSize(vlog_files);
              tail = utils::offset_tail(vlog_files, head);
          } else {
              head = 0;
              tail = 0;
          }

          if (FileExists(filename)) {
              RandomReadableFile *file;
              NewRandomReadableFile(filename, &file);
              Slice slice;
              char buf[40];
              file->Read(0, 40, &slice, buf);
              if (slice.size() == 40) {
                  fileno = DecodeFixed64(buf);
                  timestamp_ = DecodeFixed64(buf + 8);
                  max_level = DecodeFixed64(buf + 32);
              }
              delete file;
          } else {
              WriteToFile(this);
          }
          EmplaceStatus();
      }

      ~Version() {
          // write into current file
          WriteToFile(this);
      }

      static inline void WriteToFile(const Version *v) {
          WritableNoBufFile *file;
          NewWritableNoBufFile(v->filename, &file);

          char buf[40];
          EncodeFixed64(buf, v->fileno);
          EncodeFixed64(buf + 8, v->timestamp_);
          EncodeFixed64(buf + 16, v->head);
          EncodeFixed64(buf + 24, v->tail);
          EncodeFixed64(buf + 32, v->max_level);
          file->WriteUnbuffered(buf, 40);
          delete file;
      }

      void EmplaceStatus() {
          // IF ONLY ADD TWO NEW LEVEL WILL NOT REVERSE
          max_level = max_level % 1000;
          status.reserve(max_level + 5);
          for (uint64_t i = 0; i <= max_level; i++) {
              // TODO MAYBE OVERFLOW (?)
              status.emplace_back();
          }
      }

      void reset() {
          fileno = 0;
          timestamp_ = 1;
          head = 0;
          tail = 0;
          max_level = 7;
          status.clear();
          WriteToFile(this);
          EmplaceStatus();
      }

      [[nodiscard]] std::string DBName() const {
          return Dirname(filename);
      }

      void LoadStatus(uint64_t level, uint64_t file_no) {
          status[level].insert(file_no);
      }

      void ClearLevelStatus(uint64_t level, std::vector<uint64_t> old_files[2]) {
          std::string dir_name = DBDirName(filename);
          std::vector<std::future<void>> tasks;
          WriteSlice s(dir_name.data(), 0);
          tasks.reserve(old_files[0].size() + old_files[1].size());
          for (int i = 0; i < 2; ++i) {
              for (auto &it: old_files[i]) {
                  status[level + i].erase(it);
                  Request req = {false, SSTFilePath(dir_name, level + i, it), s};

                  tasks.emplace_back(std::move(SubmitWrite(std::move(req))));
              }
          }
          for (auto &fu: tasks) {
              fu.get();
          }
      }

      void RemoveFile(const std::string &file_name) {

      }

      std::future<void> SubmitWrite(Request &&req) {
          return write_scheduler_.submit([reqs = std::move(req)]() {
              WriteSchedule(reqs);
          });
      }

      // they are moved
      void MoveLevelStatus(uint64_t level, std::vector<uint64_t> &old_files) {
          for (auto &it: old_files) {
              status[level].erase(it);
              status[level + 1].insert(it);
          }
      }

      uint32_t NumLevelFiles(uint32_t level) const {
          return status[level].size();
      }

      uint32_t MaxLevelFiles(uint32_t level) const {
          return level == 0 ? Option::kL0_CompactionTrigger : 1 << (level + 1);
      }

      bool LevelOver(uint64_t level) {
          return status[level].size() > (1 << (level + 1));
      }

      void AddNewLevelStatus(uint64_t level, uint64_t file_no, uint64_t size) {
          for (uint64_t i = 0; i < size; i++) {
              status[level].insert(file_no + i);
          }
      }

      [[nodiscard]] uint64_t GetTreeLevel() const {
          return max_level;
      }

      // Create New level
      void AddNewLevel(uint64_t num) {
          max_level += num;
          for (num; num > 0; --num) {
              status.emplace_back();
              std::string dir_name = LevelDirName(DBName(), max_level - num + 1);
              if (!utils::dirExists(dir_name)) {
                  utils::_mkdir(dir_name);
              }
          }
      }

      void ToNextLevel(uint64_t level, std::vector<uint64_t> &new_files) {
          for (auto new_file: new_files) {
              status[level + 1].insert(new_file);
          }
      }

      [[nodiscard]] bool NeedNewLevel(uint64_t level) const {
          return max_level <= level;
      }

      std::set<uint64_t> &GetLevelStatus(uint64_t level) {
          assert(level < status.size());
          return status[level];
      }

      uint64_t NewFileNumber() {
          return last_file_no_++;
      }

      void ReuseFileNumber(uint64_t file_no) {
          if (file_no + 1 == last_file_no_) {
              last_file_no_ = file_no;
          }
      }

      void SetLastSequence(uint64_t seq) {
          assert(seq > last_sequence_);
          last_sequence_ = seq;
      }

      uint64_t LastSequence() {
          return last_sequence_;
      }

      Executor write_scheduler_;
      std::string filename;
      uint64_t fileno = 0;
      uint64_t timestamp_ = 1;
      uint64_t head = 0;
      uint64_t tail = 0;
      uint64_t max_level = 7;
      uint64_t last_sequence_ = 0;
      uint64_t last_file_no_ = 0;
      std::vector<std::set<uint64_t>> status;
  };
}

#endif //VERSION_H
