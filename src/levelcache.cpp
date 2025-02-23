#include "levelcache.h"
#include <shared_mutex>

namespace LSMKV {

  LevelCache::LevelCache(std::string db_path, Version *v, std::shared_mutex *rwlock) : db_name_(
          std::move(db_path)), cmp_(new InternalKeyComparator()), vlog_reader_(db_name_),
                                                                                       sst_reader_(db_name_),
                                                                                       rwlock_(rwlock) {
      cache.resize(8);
//      for (int i = 0; i < 8; ++i) {
//          cache.emplace_back();
//      }
//      std::vector<std::string> dirs;
//      std::vector<std::string> files;
//      utils::scanDirs(db_name_, dirs);
//      Slice result;
//      std::unique_ptr<char[]> raw;

//      RandomReadableFile *file;
//      bool s;
//      std::string path_name = db_name_ + "/";
//      size_t dir_size;
//      size_t file_size;
//      uint64_t dir_level;
//      uint64_t cur_level = 7;
//
//      raw = std::make_unique<char[]>(16 * 1024);
//      dir_size = path_name.length();
  }

  LevelCache::~LevelCache() {
      delete cmp_;
  }

  uint64_t LevelCache::CompactionSST(uint64_t level,
                                     uint64_t file_no,
                                     uint64_t size,
                                     std::vector<uint64_t> *old_file_nos,
                                     std::vector<uint64_t> &need_to_move,
                                     std::vector<WriteSlice> &need_to_write) {
      return 0;
  }

// key = U64_MAX and timestamp_ = 0 is end
// note: the tuple is <key, timestamp_, value>
  int cmp(Table::TableIterator *lhs, Table::TableIterator *rhs) {
      // m for max, n for normal
      // m m false
      // n m true
      // m n false
      // n n return n < n

      if (lhs->isEnd()) {
          return false;
      }

      if (rhs->isEnd()) {
          return true;
      }

      return lhs->merge_key() < rhs->merge_key();
  }

  struct LoserTree {
      LoserTree(size_t n, std::vector<std::unique_ptr<Table::TableIterator>> &m_way) : n_(n), m_way_(m_way) {
          std::vector<size_t> winner;
          winner.reserve(n);
          winner.resize(n);
          size_t m_way_init = (n) / 2;
          size_t child;
          size_t i;
          for (i = n - 1; i > m_way_init; --i) {
              child = i * 2 - n;
              set_loser_winner(loser_[i], winner[i], child, child + 1);
          }

          if (n & 1) {// odd
              // left child is the last inner node, right child is first external node
              set_loser_winner(loser_[m_way_init], winner[m_way_init], winner[n - 1], 0);
          } else {
              set_loser_winner(loser_[m_way_init], winner[m_way_init], 0, 1);
          }

          for (i = m_way_init; i > 0; i /= 2)
              for (size_t j = i; j > i / 2;) {
                  --j;
                  set_loser_winner(loser_[j], winner[j], winner[2 * j], winner[2 * j + 1]);
              }
          loser_[0] = winner[1];
      }

      bool end() {
          return m_way_[top()]->timestamp() == 0;
      }

      void Adjust(size_t index) {
          size_t parent = index + n_;
          Table::TableIterator *s = m_way_[index].get();
          Table::TableIterator *l;

          size_t *ptree = loser_;
          while ((parent /= 2) > 0) {
              size_t &tparent = ptree[parent];
              if (!cmp(s, l = m_way_[tparent].get())) {
                  std::swap(tparent, index);
                  std::swap(s, l);
              }
          }
          loser_[0] = index;
      }

      static LoserTree *CreateLoser(size_t n, std::vector<std::unique_ptr<Table::TableIterator>> &m_way) {
          void *ptr = malloc(sizeof(LoserTree) + sizeof(size_t) * n);
          return new(ptr) LoserTree(n, m_way);
      }

      size_t top() {
          return loser_[0];
      }

      [[nodiscard]] auto top_iter(size_t &top) const {
          top = loser_[0];
          return m_way_[top].get();
      }

      void increment() {
          size_t top = loser_[0];
          m_way_[top]->merge_next();
          Adjust(top);
      }

      void set_loser_winner(size_t &loser, size_t &winner, size_t left, size_t right) {
          if (cmp(m_way_[left].get(), m_way_[right].get())) {
              loser = right;
              winner = left;
          } else {
              loser = left;
              winner = right;
          }
      }

      ~LoserTree() = default;

      size_t n_;
      std::vector<std::unique_ptr<Table::TableIterator>> &m_way_;
      // key[0] waits to insert
      size_t loser_[0];
  };

  void LevelCache::Merge(uint64_t file_no,
                         uint64_t level,
                         uint64_t timestamp,
                         bool isDrop,
                         std::vector<std::unique_ptr<TableIterator>> &wait_to_merge,
                         std::vector<WriteSlice> &need_to_write) {

  }

  void LevelCache::reset() {
      cache.clear();
  }

  struct UserKeyComparator : public ComparatorImpl<UserKeyComparator> {
      static int compare_impl(const Slice &a, const Slice &b) {
          return StrComparator::compare_impl(ExtractUserKey(a), ExtractUserKey(b));
      }

      static void find_shortest_separator_impl(std::string *start, const Slice &limit) {
          assert(false);
          return;
      }

      static void find_short_successor_impl(std::string *key) {
          assert(false);
          return;
      }

      static const char *name_impl() {
          return "UserKeyComparator";
      }
  };

  void LevelCache::scan(const Slice &K1, const Slice &K2, std::map<std::string, std::string> *key_map) {
      std::shared_lock lock(*rwlock_);
      // TODO: add binary search in each level
      const auto seq = ExtractSequenceNumber(K1);

      UserKeyComparator user_cmp;

      {
          const auto &level0 = cache[0];

          // in level0, the bigger file_number_ means the key with higher timestamp
          for (const auto &it: std::ranges::reverse_view(level0)) {
              const auto &meta = it.second;
              auto table = meta.NewIterator(db_name_, &user_cmp);
              table->seek(K1, K2);
              Scan(key_map, table, seq);
              delete table;
          }
      }

      // in other levels, the smaller file_number_ means the key with higher timestamp
      // cause of the compaction, after compaction,
      // (the key with higher timestamp) 's file_number_ <= (the key with lower timestamp) 's file_number_
      for (auto i = 1; i < cache.size(); ++i) {
          assert(false);
          const auto &level = cache[i];
          for (const auto &it: std::ranges::reverse_view(level)) {
              const auto &meta = it.second;
              auto table = meta.NewIterator(db_name_, &user_cmp);
              table->seek(K1, K2);

              Scan(key_map, table, seq);

              delete table;
          }
      }
  }

  uint64_t BinarySearchSST(const char *start, Slice key, uint64_t size) {
      uint64_t left = 0, right = size, mid;
      while (left < right) {
          mid = left + ((right - left) >> 1);
          // TODO: variable length key
          if (Slice(start + mid * 20, 8) < key)
              left = mid + 1;
          else
              right = mid;
      }
      // To avoid False positive
      // TODO: variable length key
      if (left <= size && Slice(start + left * 20, 8) == key) {
          return left;
      }

      return size;
  }

  Status LevelCache::get(const QueryKey &key, VLogEntryInfo *info) {
      std::shared_lock lock(*rwlock_);

      Status s = Status::NotFound();

      {
          const auto &level0 = cache[0];
          std::vector<const SSTFileMeta *> tmp;

          for (const auto &it: std::ranges::reverse_view(level0)) {
              if (it.second.InRange(key)) {
                  tmp.emplace_back(std::addressof(it.second));
              }
          }


          if (!tmp.empty()) {
              s = sst_reader_.ReadBatch(tmp, key.internal_key(), info);
          }
      }

      if (s.IsNotFound()) {
          for (auto i = 1; i < cache.size(); ++i) {
              const auto &level = cache[i];
              std::vector<const SSTFileMeta *> tmp;

              for (const auto &it: std::ranges::reverse_view(level)) {
                  if (it.second.InRange(key)) {
                      tmp.emplace_back(std::addressof(it.second));
                  }
              }

              if (!tmp.empty()) {
                  s = sst_reader_.ReadBatch(tmp, key.internal_key(), info);
              }
          }

      }

      return s;
  }

  bool LevelCache::empty() const {
      return cache.empty();
  }

  Status LevelCache::GetOffset(const Slice &key, uint64_t &offset) {
//      TableIterator *table;
//      std::shared_lock lock(*rwlock_);
//      for (auto &level: cache) {
//          for (const auto &it: std::ranges::reverse_view(level)) {
//              table = it.second;
//              table->seek(key);
//              if (table->hasNext()) {
//                  // if newest of value is deleted, no need to restore when gc
//                  if (DecodeFixed32(table->value().data() + 8) == 0) {
//                      return Status::NotFound();
//                  }
//                  offset = DecodeFixed64(table->value().data());
//                  return Status::OK();
//              }
//          }
//      }
      return Status::NotFound();
  }

  void LevelCache::AddFile(SSTFileMeta *f) {
      if (f->level_ >= cache.size()) {
          cache.emplace_back();
      }

      cache[f->level_].emplace(f->file_number_, std::move(*f));
  }

  template<typename function>
  void LevelCache::FindCompactionNextLevel(uint64_t level, const function &callback) {
      for (auto rit = cache[level].begin(); rit != cache[level].end(); ++rit) {
          if (callback(rit)) {
              break;
          }
      }
  }

}// namespace LSMKV