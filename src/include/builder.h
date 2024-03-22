#ifndef BUILDER_H
#define BUILDER_H

#include "levelcache.h"
#include "utils/filemeta.h"
#include "version.h"
#include <string>
#include "utils/comparator.h"
#include "db_info.h"

namespace LSMKV {
  class Iterator;

  status BuildTable(const DB_Info &dbinfo, Version *v, Iterator *iter, size_t size, LevelCache *kc);

  bool SSTCompaction(uint64_t level, uint64_t file_no, Version *v, LevelCache *kc);

  bool MoveToNewLevel(uint64_t level, const uint64_t &timestamp, std::vector<uint64_t> &new_files, Version *v);

  bool WriteSlice(std::vector<class WriteSlice> &need_to_write, uint64_t level, Version *v);

  uint64_t FindLevels(const std::string &dbname, Version *v);

  struct Builder {
      explicit Builder(const DB_Info &db_info, Version *v, LevelCache *kc) : db_info_(db_info), v_(v), kc_(kc) {
      }

      void operator()() const {
          BuildTable(db_info_, v_, it_, size_, kc_);
      }

      void setAll(size_t size, Iterator *it) {
          this->size_ = size;
          this->it_ = it;
      }

      auto create_operator() {
          return [this]() { BuildTable(db_info_, v_, it_, size_, kc_); };
      }

      size_t size_{};
      Iterator *it_{};
      const DB_Info &db_info_;
      Version *v_;
      LevelCache *kc_;
  };

  class TableBuilder {
  public:
      TableBuilder(WritableFile *file) : file_(file) {
      }

      void Add(Slice key) {

      }


  private:
      WritableFile *file_;

  };

}// namespace LSMKV

#endif//BUILDER_H
