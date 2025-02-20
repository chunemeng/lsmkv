#ifndef BUILDER_H
#define BUILDER_H

#include <string>


#include "levelcache.h"
#include "version.h"
#include "utils/comparator.h"
#include "db_info.h"
#include "format.h"


namespace LSMKV {

  class Iterator;

  Status BuildTable(const DB_Info &dbinfo, Version *v, Iterator *iter, SSTFileMeta *meta);

  Status SSTCompaction(uint64_t level, uint64_t file_no, Version *v, LevelCache *kc);

  bool MoveToNewLevel(uint64_t level, const uint64_t &timestamp, std::vector<uint64_t> &new_files, Version *v);

  bool WriteSlice(std::vector<class WriteSlice> &need_to_write, uint64_t level, Version *v);

  uint64_t FindLevels(const std::string &dbname, Version *v);

  class FilterBlockBuilder {

  public:
      FilterBlockBuilder(size_t size) {
          buffer_.resize(size);
      }

      void Flush() {
          if (keys_.empty()) {
              return;
          }

          CreateFilter(keys_.data(), keys_.size(), &buffer_);
          keys_.clear();
      }

      Slice Finish() {
          Flush();
          return {buffer_};
      }


      void AddKey(const Slice &key) {
          // Save the key
          keys_.push_back(key);
      }

  private:

      std::vector<Slice> keys_;
      std::string buffer_;
  };

  class BlockBuilder {
  public:
      BlockBuilder(const Comparator *cmp) : comparator_(cmp) {
      }

      size_t BlockSize() const {
          return buffer_.size();
      }

      void Add(const Slice &key, const Slice &value) {
          auto sz = buffer_.size();
          // key size 4
          // value size 1
          buffer_.resize(sz + 5);
          auto buf = buffer_.data() + sz;

          // key size and value size then key and value
          EncodeFixed32(buf, key.size());
          EncodeFixed8(buf + 4, value.size());

          buffer_.append(key.data(), key.size());
          buffer_.append(value.data(), value.size());
      }

      bool Empty() const {
          return buffer_.empty();
      }

      Slice Finish() {
          return buffer_;
      }

      void Reset() {
          buffer_.clear();
      }

  private:
      std::string buffer_;

      const Comparator *comparator_;
  };

  class TableBuilder {
  private:
      static std::shared_ptr<Executor> GetExecutor() {
          static std::shared_ptr executor = std::make_shared<Executor>(1);
          return executor;
      }

      Slice DataBlockEntryInfo() {
          return {reinterpret_cast<char *>(&data_block_entry_), sizeof(BlockEntryInfo)};
      }

      void WriteRawBlock(Slice raw, BlockEntryInfo *handle) {
          handle->size_ = raw.size();
          handle->offset_ = offset_;

          status_ = file_->Append(raw);
          if (status_.ok()) {
              char trailer[Option::kBlockTrailerSize];
              uint16_t crc = utils::crc16(raw.data(), raw.size());
              EncodeFixed16(trailer, crc);
              status_ = file_->Append(Slice(trailer, Option::kBlockTrailerSize));
              if (status_.ok()) {
                  offset_ += raw.size() + Option::kBlockTrailerSize;
              }
          }
      }

      void WriteBlock(BlockBuilder *block, BlockEntryInfo *handle) {
          if (block->Empty()) {
              return;
          }

          Slice raw = block->Finish();


          WriteRawBlock(raw, handle);

          block->Reset();
      }

  public:
      TableBuilder(WritableFile *file, Comparator *cmp) : file_(file),
                                                          comparator_(cmp), data_block_(cmp), index_block_(cmp) {
          filter_block_ = new FilterBlockBuilder(Option::bloom_size_);
      }

      ~TableBuilder() {
          if (!closed_) {
              Finish();
          }
          if (filter_block_) {
              delete filter_block_;
              filter_block_ = nullptr;
          }
      }

      void Add(const Slice &key, const Slice &value);

      void Flush();

      bool OK() const { return status_.ok(); }

      Status status() const { return status_; }

      Status Finish();

      uint64_t FileSize() const {
          return offset_;
      }

      uint64_t NumEntries() const {
          return num_entries_;
      }


  private:
      WritableFile *file_;

      bool need_next_index_entry_{false};

      BlockBuilder index_block_;
      BlockBuilder data_block_;

      BlockEntryInfo data_block_entry_{0, 0};


      size_t num_entries_{0};

      Status status_;

      bool closed_{false};

      uint64_t offset_{0};

      Slice last_key_;

      FilterBlockBuilder *filter_block_{};
      const Comparator *comparator_;
  };

}// namespace LSMKV

#endif//BUILDER_H
