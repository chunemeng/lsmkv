#ifndef LSMKV_SRC_KEYCACHE_H
#define LSMKV_SRC_KEYCACHE_H

#include <list>
#include <map>
#include <queue>
#include <ranges>
#include <set>
#include <unordered_set>
#include <shared_mutex>

#include "format.h"
#include "utils/utils.h"
#include "utils/status.h"
#include "utils/rwlock.h"
#include "memtable.h"
#include "table.h"
#include "version.h"
#include "lsmkv/dbformat.h"

namespace LSMKV {
  class LevelCache {
      typedef Table::TableIterator TableIterator;
  public:
      LevelCache(const LevelCache &) = delete;

      const LevelCache &operator=(const LevelCache &) = delete;

      explicit LevelCache(std::string db_path, Version *v, std::shared_mutex *rwlock);

      // return timestamp_ to update the version
      uint64_t CompactionSST(uint64_t level,
                             uint64_t file_no,
                             uint64_t size,
                             std::vector<uint64_t> old_file_nos[2],
                             std::vector<uint64_t> &need_to_move,
                             std::vector<WriteSlice> &need_to_write);

      template<typename function>
      void FindCompactionNextLevel(uint64_t level, function const &callback);

      void Merge(uint64_t file_no,
                 uint64_t level,
                 uint64_t timestamp,
                 bool isDrop,
                 std::vector<std::unique_ptr<TableIterator>> &wait_to_merge,
                 std::vector<WriteSlice> &need_to_write);

      void reset();

      void scan(const Slice &K1, const Slice &K2, std::map<std::string, std::string> &key_map);

      Status get(const QueryKey &key, VLogEntryInfo *val);

//      std::string get(uint64_t key) {
//          char char_key[8];
//          EncodeFixed64(char_key, key);
//          Slice ke(char_key, 8);
//          std::string val;
//          get(ke, val);
//          return val;
//      }

      ~LevelCache();

      bool empty() const;

      Status GetOffset(const Slice &key, uint64_t &offset);

      void AddFile(int level, uint64_t file, uint64_t file_size,
                   const InternalKey &smallest, const InternalKey &largest);

  private:
      const std::string db_name_;
      std::shared_mutex * rwlock_;

      Comparator *const cmp_;
      VLogReader vlog_reader_;
      SSTReader sst_reader_;

      std::vector<std::map<uint64_t, SSTFileMeta>> cache;
  };
}

#endif //LSMKV_SRC_KEYCACHE_H
