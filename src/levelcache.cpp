#include "levelcache.h"
#include "builder.h"
#include "vlog_builder.h"
#include <shared_mutex>

namespace LSMKV {
  LevelCache::LevelCache(std::string db_path, std::shared_ptr<Version> v, std::shared_mutex *rwlock) : db_name_(
          std::move(db_path)), cmp_(InternalKeyComparator()), vlog_reader_(db_name_), sst_reader_(db_name_),
                                                                                                       rwlock_(rwlock),
                                                                                                       version_(
                                                                                                               std::move(
                                                                                                                       v)) {
      NewAppendableFile(VersionFileName(db_name_), &meta_file_);
      cache.resize(8);
  }

  LevelCache::~LevelCache() = default;

  int cmp(Iterator *lhs, Iterator *rhs) {
      // m for max, n for normal
      // m m false
      // n m true
      // m n false
      // n n return n < n

      if (!lhs->valid()) {
          return false;
      }

      if (!rhs->valid()) {
          return true;
      }

      return lhs->key() < rhs->key();
  }

  struct LoserTree {
      LoserTree(size_t n, std::vector<std::unique_ptr<Iterator>> &m_way, Comparator *cmp) : n_(n), m_way_(m_way),
                                                                                            cmp_(cmp) {
          loser_.resize(n);
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
              // left child is the last inner node, right child is the first external node
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
          return !m_way_[top()]->valid();
      }

      void Adjust(size_t index) {
          size_t parent = index + n_;
          Iterator *s = m_way_[index].get();
          Iterator *l;

          size_t *ptree = loser_.data();
          while ((parent /= 2) > 0) {
              size_t &tparent = ptree[parent];
              if (!cmp(s, l = m_way_[tparent].get())) {
                  std::swap(tparent, index);
                  std::swap(s, l);
              }
          }
          loser_[0] = index;
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
          m_way_[top]->next();
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
      Comparator *cmp_;

      std::vector<std::unique_ptr<Iterator>> &m_way_;
      // key[0] waits to insert
      std::vector<size_t> loser_;
  };

  void LevelCache::reset() {
      cache.clear();
  }

  Status LevelCache::gc() {
      return Status::OK();
  }

  void LevelCache::scan(const Slice &K1, const Slice &K2, std::map<std::string, std::string> *key_map) {
      std::shared_lock lock(*rwlock_);
      // TODO: add binary search in each level
      // TODO: add coroutine support iterator
      const auto seq = ExtractSequenceNumber(K1);

      Comparator cmp = UserKeyComparator();

      {
          const auto &level0 = cache[0];

          // in level0, the bigger file_number_ means the key with higher timestamp
          for (const auto &it: std::ranges::reverse_view(level0)) {
              const auto &meta = it.second;
              auto table = meta.NewIterator(db_name_, &cmp);
              table->seek(K1, K2);
              Scan(key_map, table, seq);
              delete table;
          }
      }

      // in other levels, the smaller file_number_ means the key with higher timestamp
      // cause of the compaction, after compaction,
      // (the key with higher timestamp) 's file_number_ < = (the key with lower timestamp) 's file_number_
      for (auto i = 1; i < cache.size(); ++i) {
          const auto &level = cache[i];
          for (const auto &it: level) {
              const auto &meta = it.second;
              auto table = meta.NewIterator(db_name_, &cmp);
              table->seek(K1, K2);

              Scan(key_map, table, seq);

              delete table;
          }
      }
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

              for (const auto &it: level) {
                  if (it.second.InRange(key)) {
                      tmp.emplace_back(std::addressof(it.second));
                  }
              }

              if (!tmp.empty()) {
                  // there still need batch read
                  // cause the mvcc key may overlap in different files
                  s = sst_reader_.ReadBatch(tmp, key.internal_key(), info);

                  if (s.ok()) {
                      break;
                  }
              }
          }

      }

      if (!s.ok() && !s.IsNotFound()) {
          log::error("get failed: {}", s.ToString());
          return s;
      }

      return s;
  }

  bool LevelCache::empty() const {
      return cache.empty();
  }

  void LevelCache::AddFile(uint32_t level, SSTFileMeta *f) {
      if (level >= cache.size()) {
          cache.resize(level + 1);
      }

      cache[level].emplace(f->file_number_, std::move(*f));
  }

  void LevelCache::AddFile(uint32_t level, std::vector<SSTFileMeta *> &files) {
      if (level >= cache.size()) {
          cache.resize(level + 1);
      }


      for (auto &file: files) {
          cache[level].emplace(file->file_number_, std::move(*file));
      }
  }

  Status LevelCache::MoveFiles(uint32_t level, const std::vector<SSTFileMeta *> &metas, uint32_t new_level) {
      if (level >= cache.size()) {
          return Status::Corruption("level out of range");
      }

      if (new_level >= cache.size()) {
          cache.resize(new_level + 1);
      }

      auto &level_cache = cache[level];
      auto &new_level_cache = cache[new_level];

      for (auto &meta: metas) {
          auto it = level_cache.find(meta->file_number_);
          if (it == level_cache.end()) {
              return Status::NotFound();
          }
          it->second.level_ = new_level;

          new_level_cache.emplace(meta->file_number_, std::move(it->second));
          level_cache.erase(it);
      }

      return Status::OK();
  }


  Status LevelCache::RemoveFileAtCompaction(uint32_t level, const std::vector<SSTFileMeta *> &metas) {
      Status s = Status::OK();
      for (auto &meta: metas) {
          s = RemoveFile(level, meta->file_number_);
          if (!s.ok()) {
              break;
          }
      }
      return s;
  }


  Status LevelCache::PickCompaction(CompactInfo *compact_info) {
      auto level = compact_info->level;

      if (level >= cache.size()) {
          return Status::OK();
      }

      auto pick_file_num = PickSSTFileNum(level);

      assert(level != 0 || pick_file_num == cache[0].size());

      if (pick_file_num == 0) {
          return Status::OK();
      }

      auto &level_cache = cache[level];

      for (auto &it: level_cache) {
          if (compact_info->old_files.empty()) [[unlikely]] {
              compact_info->smallest_key = &it.second.smallest;
              compact_info->largest_key = &it.second.largest;
          } else {
              if (cmp_.compare(it.second.smallest.Encode(), compact_info->smallest_key->Encode()) < 0) {
                  compact_info->smallest_key = &it.second.smallest;
              }

              if (cmp_.compare(it.second.largest.Encode(), compact_info->largest_key->Encode()) > 0) {
                  compact_info->largest_key = &it.second.largest;
              }
          }

          compact_info->old_files.emplace_back(std::addressof(it.second));
          if (compact_info->old_files.size() == pick_file_num) {
              break;
          }
      }

      return PickOverlappingFiles(compact_info);
  }

  Status LevelCache::PickOverlappingFiles(CompactInfo *compact_info) {
      auto level = compact_info->level + 1;
      if (level >= cache.size()) {
          return Status::OK();
      }

      auto &level_cache = cache[level];
      Comparator cmp = InternalKeyComparator();

      for (auto &it: level_cache) {
          if (it.second.InRange(*compact_info->smallest_key, *compact_info->largest_key, &cmp)) {
              compact_info->overlap_files.emplace_back(std::addressof(it.second));
          }
      }

      return Status::OK();
  }

  Status LevelCache::DoCompactionWork(CompactInfo *compact_info) {
      auto level = compact_info->level;

      if (level >= cache.size()) {
          return Status::Corruption("level out of range");
      }

      // >= kCompactionVLogLevel level, we need build vlog
      // plus because we write to next level, we need to build vlog
      bool compact_vlog = (level + 1 >= Option::kCompactionVLogLevel);

      if (level != 0 && !compact_vlog && compact_info->overlap_files.empty()) {
          return MoveFiles(level, compact_info->old_files, level + 1);
      }


      std::vector<std::unique_ptr<Iterator>> wait_to_merge;
      std::vector<uint32_t> rm_vlog_files;
      std::vector<uint32_t> rm_sst_files;

      for (auto &f: compact_info->old_files) {
          std::unique_ptr<Iterator> iter(f->NewIterator(db_name_, &cmp_));
          iter->seekToFirst();
          rm_sst_files.emplace_back(f->file_number_);
          if (level >= Option::kCompactionVLogLevel) {
              assert(f->vlog_file_no_ != 0);
              rm_vlog_files.emplace_back(f->vlog_file_no_);
          }

          wait_to_merge.emplace_back(std::move(iter));
      }


      for (auto &f: compact_info->overlap_files) {
          std::unique_ptr<Iterator> iter(f->NewIterator(db_name_, &cmp_));
          iter->seekToFirst();
          rm_sst_files.emplace_back(f->file_number_);
          if (compact_vlog) {
              assert(f->vlog_file_no_ != 0);
              rm_vlog_files.emplace_back(f->vlog_file_no_);
          }

          wait_to_merge.emplace_back(std::move(iter));
      }

      std::vector<SSTFileMeta> new_files;
      Status s = BuildWhenCompaction(&wait_to_merge, &new_files, compact_vlog);

      if (!s.ok()) {
          return s;
      }

      RemoveFileAtCompaction(level, compact_info->old_files);
      RemoveFileAtCompaction(level + 1, compact_info->overlap_files);

      version_->executor_->submit(
              [db_name = db_name_, sst_files = std::move(rm_sst_files), vlog_files = std::move(rm_vlog_files)]() {
                  for (auto &f: sst_files) {
                      auto file_name = SSTFileName(db_name, f);
                      utils::rmfile(file_name);
                  }
                  for (auto &f: vlog_files) {
                      auto file_name = VLogFileName(db_name, f);
                      utils::rmfile(file_name);
                  }
              });

      AddFile(level + 1, new_files);

      return Status::OK();

  }

  Status LevelCache::RemoveFile(uint32_t level, uint64_t file_no) {
      if (level >= cache.size()) {
          return Status::Corruption("level out of range");
      }

      auto &level_cache = cache[level];

      auto it = level_cache.find(file_no);

      if (it == level_cache.end()) {
          return Status::NotFound();
      }

      level_cache.erase(it);
      return Status::OK();
  }

  Status
  LevelCache::BuildWhenCompaction(std::vector<std::unique_ptr<Iterator>> *wait_to_merge,
                                  std::vector<SSTFileMeta> *new_files,
                                  bool build_vlog) {

      LoserTree loser_tree(wait_to_merge->size(), *wait_to_merge, &cmp_);

      Status status = Status::OK();
      std::unique_ptr<WritableFile> file;

      std::unique_ptr<TableBuilder> builder;

      std::unique_ptr<VLogBuilder> vlog_builder;

      VLogReader vlog_reader(db_name_);

      std::string vlog_buf;

      std::optional<SSTFileMeta> meta;

      Slice key;

      for (; !loser_tree.end(); loser_tree.increment()) {
          auto top = loser_tree.top();
          auto iter = loser_tree.top_iter(top);
          key = iter->key();
          auto value = iter->value();

          if (!meta.has_value()) {
              meta = SSTFileMeta{};
              meta->smallest.DecodeFrom(key);
          }


          auto seq_no = ExtractSequenceNumber(key);
          if (seq_no < version_->LastLivingSequence()) {
              continue;
          }

          auto value_type = ExtractValueType(key);

          VLogEntryInfo vlog_entry_info{};

          Status s = Status::OK();

          if (file == nullptr) {
              auto file_number = version_->NewSSTFileNumber();
              auto fname = SSTFileName(db_name_, file_number);
              s = NewUringWritableFile(fname, &file);
              builder = std::make_unique<TableBuilder>(file.get(), &cmp_, version_->executor_);

              if (build_vlog) {
                  if (vlog_builder == nullptr) {
                      vlog_builder = std::make_unique<VLogBuilder>(db_name_, version_->executor_);
                  }

                  auto f_no = version_->NewVLogFileNumber();
                  s = vlog_builder->Open(f_no);
                  if (!s.ok()) {
                      version_->ReuseVLogFileNumber(f_no);
                      return s;
                  }
              }
          }

          if (!s.ok()) {
              return s;
          }

          if (value_type != kTypeValue) {
              builder->Add(key, {});
          } else {
              if (build_vlog) {
                  s = vlog_entry_info.Decode(value);

                  if (!s.ok()) {
                      return s;
                  }

                  s = vlog_reader.Read(vlog_entry_info,
                                       &vlog_buf);
                  if (!s.ok()) {
                      return s;
                  }

                  s = vlog_builder->Append(key, vlog_buf, &vlog_entry_info, false);
                  if (!s.ok()) {
                      return s;
                  }
                  value = vlog_entry_info.ToSlice();
              }

              builder->Add(key, value);

              if (!builder->OK()) {
                  return builder->status();
              }
          }

          if (builder->ApproximateFileSize() >= Option::sst_file_size_) {
              auto f_number = version_->NewSSTFileNumber();
              meta->file_number_ = f_number;
              meta->largest.DecodeFrom(key);
              builder->SetLastKey(key);
              s = builder->Finish();

              if (!s.ok()) [[unlikely]] {
                  return s;
              }

              meta->file_size_ = builder->FileSize();


              s = file->Sync();
              if (!s.ok()) [[unlikely]] {
                  return s;
              }

              s = file->Close();
              if (!s.ok()) [[unlikely]] {
                  return s;
              }

              builder.reset();
              file.reset();
              version_->SetSSTFileNumber(f_number + 1);


              if (build_vlog) {
                  meta->vlog_file_no_ = vlog_builder->file_number();

                  s = vlog_builder->Close();

                  if (!s.ok()) [[unlikely]] {
                      return s;
                  }
              }

              new_files->emplace_back(std::move(meta.value()));
              meta.reset();
          }

          if (!status.ok()) [[unlikely]] {
              return status;
          }
      }

      if (builder != nullptr) {
          auto f_number = version_->NewSSTFileNumber();
          meta->file_number_ = f_number;
          meta->largest.DecodeFrom(key);

          builder->SetLastKey(key);
          Status s = builder->Finish();
          meta->file_size_ = builder->FileSize();
          if (!s.ok()) {
              return s;
          }

          s = file->Sync();
          if (!s.ok()) [[unlikely]] {
              return s;
          }

          s = file->Close();
          if (!s.ok()) [[unlikely]] {
              return s;
          }

          builder.reset();
          file.reset();
          version_->SetSSTFileNumber(f_number + 1);


          if (build_vlog) {
              meta->vlog_file_no_ = vlog_builder->file_number();

              s = vlog_builder->Close();

              if (!s.ok()) {
                  return s;
              }

              vlog_builder.reset();
          }

          new_files->emplace_back(std::move(meta.value()));
          meta.reset();
      }

      if (vlog_builder != nullptr) {
          assert(vlog_builder->Empty());
      }
      wait_to_merge->clear();

      return Status::OK();
  }

  void LevelCache::AddFile(uint32_t level, std::vector<SSTFileMeta> &files) {
      if (level >= cache.size()) {
          cache.resize(level + 1);
      }

      auto &level_cache = cache[level];

      for (auto &f: files) {
          if (level != 0 && level >= Option::kCompactionVLogLevel) {
              assert(f.vlog_file_no_ != 0);
          }

          f.level_ = level;
          level_cache.emplace(f.file_number_, std::move(f));
      }

  }

}// namespace LSMKV