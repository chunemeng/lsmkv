#ifndef VERSION_H
#define VERSION_H

#include "utils/bloomfilter.h"
#include "utils/coding.h"
#include "utils/file.h"
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
          EmplaceStatus();
      }

      ~Version() = default;

      static inline void WriteToFile(const Version *v) {
          WritableNoBufFile *file;
          NewWritableNoBufFile(v->filename, &file);

          char buf[40];
          EncodeFixed64(buf, v->fileno_);
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
          fileno_ = 0;
          last_vlog_file_no_ = 0;
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

      uint64_t NewVLogFileNumber() {
          return last_vlog_file_no_.fetch_add(1);
      }

      uint64_t NewSSTFileNumber() {
          return fileno_;
      }

      uint64_t ReuseSSTFileNumber(uint64_t file_no) {
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
          last_sequence_ = seq;
      }

      uint64_t LastSequence() const {
          return last_sequence_;
      }

      std::string filename;
      uint64_t fileno_ = 0;

      uint64_t max_level = 7;
      uint64_t last_sequence_ = 0;
      std::atomic<uint64_t> last_vlog_file_no_ = 0;

      std::shared_ptr<Executor> executor_ = std::make_shared<Executor>(2);

      std::vector<std::set<uint64_t>> status;
  };
}

#endif //VERSION_H
