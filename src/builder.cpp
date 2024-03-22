#include "builder.h"
#include "version.h"
#include "vlogbuilder.h"
#include "filemeta.h"

namespace LSMKV {

  status BuildTable(const DB_Info &db_info, Version *v, Iterator *iter, size_t size, LevelCache *kc) {
      SSTFileMeta meta{};
      meta.file_size_ = size;
      iter->seekToFirst();
      std::string vlog_buf;
      char *key_buf;
      uint64_t value_size{}, key_offset{};
      uint64_t level = FindLevels(db_info.dbname, v);

      WritableFile *sst_file = nullptr;

      bool compaction = false;
      uint64_t head_offset = v->head;

      if (iter->hasNext()) {
          WritableFile *file;

          std::string fname = SSTFileName(LevelDirName(db_info.dbname, level), v->fileno);


          status s = NewWritableFile(fname, &file);
          if (!s.ok()) {
              return s;
          }
          meta.file_size_ = size;
          meta.file_number_ = v->fileno;
          meta.smallest.DecodeFrom(iter->key());
          TableBuilder builder{file};


          v->AddNewLevelStatus(level, v->fileno, 1);

          if (v->LevelOver(level)) {
              compaction = true;
          }

          int bloom_length = bloom_size + 32;
          // TODO: variable length key
//          key_buf = kc->ReserveCache(meta.size * 28 + bloom_length, meta.size, v->fileno);

          // NEED TO CLEAR FOR BLOOM_FILTER
//          memset(key_buf + 32, 0, bloom_length - 32);

          key_offset = bloom_length;

          assert(iter->key().size() == 16 && "change after variable length key");
          meta.smallest.DecodeFrom(iter->key());
          Slice key{};
          Slice val{};
          for (; iter->hasNext(); iter->next()) {
              key = iter->key();

              memcpy(key_buf + key_offset, key.data(), key.size());
              key_offset += key.size();
              EncodeFixed64(key_buf + key_offset, head_offset);
              EncodeFixed32(key_buf + key_offset + 8, value_size);
              head_offset += value_size ? value_size + 15 : 0;
              key_offset += 12;
          }

          auto fu = default_scheduler().submit([buf = key_buf, length = bloom_length, sz = meta.size]() {
              CreateFilter(buf + length, sz, 20, buf + 32);
          });


          assert(key.size() == 16);
          meta.largest = DecodeFixed64(ExtractUserKey(key).data());

          EncodeFixed64(key_buf, v->timestamp_++);
          EncodeFixed64(key_buf + 8, meta.size);
          EncodeFixed64(key_buf + 16, meta.smallest);
          EncodeFixed64(key_buf + 24, meta.largest);

          fu.get();
          file->WriteUnbuffered(key_buf, bloom_length + meta.size * 20);

          kc->PushCache(key_buf);

          vLogBuilder.Drop();

          v->head = head_offset;
          delete file;
          v->fileno++;

          if (compaction) {
              SSTCompaction(level, v->fileno, v, kc);
          }

          Version::WriteToFile(v);
          return true;
      }
      return false;
  }

  bool SSTCompaction(uint64_t level, uint64_t file_no, Version *v, LevelCache *kc) {
      std::vector<uint64_t> need_to_move;
      //Need to be rm and earse in version
      std::vector<uint64_t> old_files[2] = {std::vector<uint64_t>(), std::vector<uint64_t>()};
      std::vector<class WriteSlice> need_to_write;

      if (v->NeedNewLevel(level)) {
          v->AddNewLevel(1);
      }
      auto size = v->LevelSize(level) - ((level == 0) ? 0 : (1 << (level + 1)));
      uint64_t timestamp = kc->CompactionSST(level, file_no, size,
                                             old_files,
                                             need_to_move,
                                             need_to_write);
      // move
      MoveToNewLevel(level, timestamp, need_to_move, v);
      v->MoveLevelStatus(level, need_to_move);
      // write
      WriteSlice(need_to_write, level, v);
      // update level status
      v->AddNewLevelStatus(level + 1, v->fileno - need_to_write.size(), need_to_write.size());
      // remove old sst
      v->ClearLevelStatus(level, old_files);

      // PASS THE COMPACTION
      if (v->LevelOver(level + 1)) {
          SSTCompaction(level + 1, v->fileno, v, kc);
      }
      return true;
  }

  bool WriteSlice(std::vector<class WriteSlice> &need_to_write, uint64_t level, Version *v) {
      auto dbname = v->DBName();
      std::vector<std::future<void>> tasks;
      tasks.reserve(need_to_write.size());
      for (auto &s: need_to_write) {
          Request req = {true, SSTFilePath(dbname, level + 1, v->fileno++), s};
          tasks.emplace_back(std::move(v->SubmitWrite(std::move(req))));
      }
      for (auto &fu: tasks) {
          fu.get();
      }
      return true;
  }

  bool MoveToNewLevel(uint64_t level, const uint64_t &timestamp, std::vector<uint64_t> &new_files, Version *v) {
      std::string dbname = v->DBName();
      if (v->NeedNewLevel(level)) {
          v->AddNewLevel(1);
      }

      for (auto &it: new_files) {
          utils::mvfile(SSTFilePath(dbname, level, it), SSTFilePath(dbname, level + 1, it));
      }

      WritableNoBufFile *file;
      char buf[8];

      auto level_dir = LevelDirName(dbname, level);

      if (!utils::dirExists(level_dir)) [[unlikely]] {
          utils::mkdir(level_dir);
      }


      // Change the timestamp_
      for (auto &it: new_files) {
          NewWriteAtStartFile(SSTFilePath(level_dir, it), &file);
          EncodeFixed64(buf, timestamp);
          file->WriteUnbuffered(Slice(buf, 8));
          delete file;
      }
      return true;
  }

  uint64_t FindLevels(const std::string &dbname, Version *v) {
      return 0;
  }

}// namespace LSMKV
