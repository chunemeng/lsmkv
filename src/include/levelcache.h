#ifndef LSMKV_SRC_KEYCACHE_H
#define LSMKV_SRC_KEYCACHE_H

#include <list>
#include <map>
#include <queue>
#include <ranges>
#include <set>
#include <unordered_set>
#include <shared_mutex>

#include "block_format.h"
#include "utils/utils.h"
#include "utils/status.h"
#include "utils/rwlock.h"
#include "memtable.h"
#include "version.h"
#include "lsmkv/dbformat.h"
#include "utils/log.h"
#include "reader.h"

namespace LSMKV {
  static inline Status
  Scan(std::map<std::string, std::string> *key_map, Iterator *iter,
       SequenceNumber seq) {
      std::string last_key;
      if (iter->valid()) {
          do {
              auto key = iter->key();
              auto key_seq = ExtractSequenceNumber(key);
              if (key_seq <= seq) {
                  key_map->emplace(ExtractUserKey(key), iter->value());
                  last_key = ExtractUserKey(key);

                  iter->next();
                  break;
              }

              iter->next();
          } while (iter->valid());

          while (iter->valid()) {
              Slice table_key = iter->key();
              auto key_seq = ExtractSequenceNumber(table_key);

              if (key_seq <= seq && ExtractUserKey(table_key) != last_key) {

                  key_map->emplace(ExtractUserKey(table_key), iter->value());


                  last_key = ExtractUserKey(table_key);
              }

              iter->next();
          }
      }
      return Status::OK();
  }

  struct CompactInfo {
      uint64_t level;
      std::vector<SSTFileMeta *> old_files;
      std::vector<SSTFileMeta *> overlap_files;

      const InternalKey *smallest_key;

      const InternalKey *largest_key;
  };


  class LevelCache {
  private:
      uint32_t PickSSTFileNum(uint32_t level) const {
          if (level == 0) {
              // safe x
              return cache[0].size();
          }
          auto num = static_cast<uint32_t>(std::pow(10, level));
          // safe x
          return cache[level].size() < num ? 0 : num - cache[level].size() + 2;
      }

      Status
      BuildWhenCompaction(std::vector<std::unique_ptr<Iterator>> *wait_to_merge, std::vector<SSTFileMeta> *new_files,
                          bool build_vlog);


      Status Recover();

  public:
      LevelCache(const LevelCache &) = delete;

      const LevelCache &operator=(const LevelCache &) = delete;

      explicit LevelCache(std::string db_path, std::shared_ptr<Version> v, std::shared_mutex *rwlock);

      void reset();

      Status gc();

      void scan(const Slice &K1, const Slice &K2, std::map<std::string, std::string> *key_map);

      Status get(const QueryKey &key, VLogEntryInfo *val);

      Status DoCompactionWork(CompactInfo *compact_info);

      ~LevelCache();

      bool empty() const;

      void AddFile(uint32_t level, SSTFileMeta *file);

      void AddFile(uint32_t level, std::vector<SSTFileMeta> &files);

      Status RemoveFile(uint32_t level, uint64_t file_no);

      Status MoveFiles(uint32_t level, const std::vector<SSTFileMeta *> &metas, uint32_t new_level);

      Status RemoveFileAtCompaction(uint32_t level, const std::vector<SSTFileMeta *> &metas);

      Status PickCompaction(CompactInfo *compact_info);

      Status PickOverlappingFiles(CompactInfo *compact_info);

      uint32_t NumLevelFiles(uint32_t level) const {
          std::shared_lock lock(*rwlock_);
          // safe s
          if (level >= cache.size()) {
              return 0;
          }
          // safe s
          return cache[level].size();
      }

      uint32_t NumLevelFilesEx(uint32_t level) const {
          // safe x
          if (level >= cache.size()) {
              return 0;
          }
          // safe x
          return cache[level].size();
      }

  private:
      const std::string db_name_;
      std::shared_mutex *rwlock_;

      std::unique_ptr<WritableFile> meta_file_;

      Comparator cmp_;
      VLogReader vlog_reader_;
      SSTReader sst_reader_;

      std::vector<std::map<uint64_t, SSTFileMeta>> cache;

      std::shared_ptr<Version> version_;
  };
}

#endif //LSMKV_SRC_KEYCACHE_H
