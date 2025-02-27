#include "builder.h"
#include "version.h"
#include "vlog_builder.h"

namespace LSMKV {

  Status BuildTable(const DB_Info &db_info, Version *v, Iterator *iter, SSTFileMeta *meta) {
      iter->seekToFirst();
      std::string vlog_buf;

      if (iter->valid()) {
          std::unique_ptr<WritableFile> file;

          auto file_number = v->NewSSTFileNumber();

          std::string fname = SSTFileName(db_info.dbname, file_number);

          Status s = NewWritableFile(fname, &file);
          if (!s.ok()) {
              return s;
          }

          meta->file_number_ = file_number;
          meta->smallest.DecodeFrom(iter->key());
          {
              Comparator cmp = InternalKeyComparator();

              TableBuilder builder{file.get(), &cmp, v->executor_};
              Slice key;
              for (; iter->valid(); iter->next()) {
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
          }


          if (s.ok()) {
              s = file->Sync();
          }
          if (s.ok()) {
              s = file->Close();
          }

          auto sz = GetFileSize(fname);

          assert(sz == meta->file_size_);

          file.reset();

          if (s.ok() && meta->file_size_ > 0) {
              // Keep it
              v->SetSSTFileNumber(file_number + 1);
          } else {
          }
          return s;
      }
      return Status::InvalidArgument();
  }
}// namespace LSMKV
