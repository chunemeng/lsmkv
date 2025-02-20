#include "builder.h"
#include "version.h"
#include "vlog_builder.h"

namespace LSMKV {

  Status BuildTable(const DB_Info &db_info, Version *v, Iterator *iter, SSTFileMeta *meta) {
      iter->seekToFirst();
      std::string vlog_buf;

      if (iter->hasNext()) {
          WritableFile *file;

          std::string fname = SSTFileName(db_info.dbname, v->fileno);

          Status s = NewWritableFile(fname, &file);
          if (!s.ok()) {
              return s;
          }
          meta->file_number_ = v->fileno;
          meta->smallest.DecodeFrom(iter->key());
          {
              Comparator *cmp = new InternalKeyComparator();

              TableBuilder builder{file, cmp};
              Slice key;
              for (; iter->hasNext(); iter->next()) {
                  key = iter->key();
                  builder.Add(key, iter->value());
              }
              if (!key.empty()) {
                  meta->largest.DecodeFrom(key);
              }

              // Finish and check for builder errors
              s = builder.Finish();
              if (s.ok()) {
                  meta->file_size_ = builder.FileSize();
                  assert(meta->file_size_ > 0);
              }

              delete cmp;
          }


          if (s.ok()) {
              s = file->Sync();
          }
          if (s.ok()) {
              s = file->Close();
          }

          auto sz = GetFileSize(fname);

          assert(sz == meta->file_size_);

          delete file;
          file = nullptr;


//          RandomReadableFile *files;
//          NewRandomReadableFile(fname, &files);
//
//          Slice input;
//          std::string buffer;
//          buffer.resize(Footer::kEncodedLength);
//
//          files->Read(meta->file_size_ - Footer::kEncodedLength, Footer::kEncodedLength, &input, buffer.data());
//          Footer footer{};
//          s = footer.Decode(input);

//          if (s.ok()) {
//              // Verify that the table is usable
//              Iterator* it = table_cache->NewIterator(ReadOptions(), meta->number,
//                                                      meta->file_size);
//              s = it->status();
//              delete it;
//          }
//          if (!iter->status().ok()) {
//              s = iter->status();
//          }

          if (s.ok() && meta->file_size_ > 0) {
              // Keep it
              v->fileno++;
          } else {
              v->RemoveFile(fname);
          }
          return s;
      }
      return Status::InvalidArgument();
  }

  Status SSTCompaction(uint64_t level, uint64_t file_no, Version *v, LevelCache *kc) {

      Status s = Status::OK();
      //Need to be rm and earse in version
      std::vector<uint64_t> old_files[2] = {std::vector<uint64_t>(), std::vector<uint64_t>()};

      if (v->NeedNewLevel(level)) {
          v->AddNewLevel(1);
      }
      auto size = v->NumLevelFiles(level) - v->MaxLevelFiles(level);

      std::vector<uint64_t> need_to_move;
      std::vector<class WriteSlice> need_to_write;

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
      if (s.ok() && v->LevelOver(level + 1)) {
          s = SSTCompaction(level + 1, v->fileno, v, kc);
      }
      return s;
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
