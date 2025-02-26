#ifndef BUILDER_H
#define BUILDER_H

#include <string>


#include "levelcache.h"
#include "version.h"
#include "utils/comparator.h"
#include "db_info.h"
#include "block_format.h"
#include "crc32c/crc32c.h"


namespace LSMKV {

  class Iterator;

  Status BuildTable(const DB_Info &dbinfo, Version *v, Iterator *iter, SSTFileMeta *meta);

  class FilterBlockBuilder {
  public:
      FilterBlockBuilder(size_t size) {
          buffer_.resize(size);
      }

      void Flush() {
          if (keys_.empty()) {
              return;
          }
          std::vector<Slice> keys;
          keys.reserve(keys_.size());
          for (auto &key: keys_) {
              keys.emplace_back(key_buf_.data() + key.first, key.second);
          }

          CreateFilter(keys.data(), keys.size(), &buffer_);
          keys_.clear();
          key_buf_.clear();
      }

      Slice Finish() {
          Flush();
          return {buffer_};
      }


      void AddKey(const Slice &key) {
          // Save the key
          auto sz = key_buf_.size();
          key_buf_.append(key.data(), key.size());
          keys_.emplace_back(sz, key.size());
      }

      uint64_t BlockSize() const {
          return buffer_.size();
      }

  private:
      std::string key_buf_;
      std::vector<std::pair<uint32_t, uint32_t >> keys_;
      std::string buffer_;
  };


  class BlockBuilder {
  public:
      BlockBuilder() = default;

      size_t
      BlockSize() const {
          return buffer_.size();
      }

      void Add(const Slice &key, const Slice &value) {
          auto sz = buffer_.size();

          auto new_sz = key.size() + value.size() + 8;

          buffer_.resize(sz + new_sz);

          auto buf = buffer_.data() + sz;

          // key size and value size then key and value
          EncodeFixed32(buf, key.size());

          EncodeFixed32(buf + 4, value.size());

          utils::m_memcpy(buf + 8, key.data(), key.size());

          utils::m_memcpy(buf + 8 + key.size(), value.data(), value.size());
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
  };


  class TableBuilder {
  private:
      Slice DataBlockEntryInfo() {
          return {reinterpret_cast<char *>(&data_block_entry_), sizeof(BlockEntryInfo)};
      }

      void WriteRawBlock(Slice raw, BlockEntryInfo *handle) {
          handle->size_ = raw.size();
          handle->offset_ = offset_;

          std::future<uint32_t> crc_fu;
          if (executor_) {
              crc_fu = executor_->submit([raw]() {
                  return crc32c::Crc32c(raw.data(), raw.size());
              });
          }

          status_ = file_->Append(raw);
          if (status_.ok()) {
              char trailer[Option::kBlockTrailerSize];
              uint32_t crc{};

              if (executor_) {
                  crc = crc_fu.get();
              } else {
                  crc = crc32c::Crc32c(raw.data(), raw.size());
              }

              EncodeFixed32(trailer, crc);
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

          WriteRawBlock(block->Finish(), handle);

          block->Reset();
      }

  public:
      TableBuilder(WritableFile *file, Comparator *cmp, std::shared_ptr<Executor> executor, bool open_filter = true)
              : file_(file),
                comparator_(cmp),
                executor_(std::move(executor)) {
          if (open_filter) {
              filter_block_ = std::make_unique<FilterBlockBuilder>(Option::bloom_size_);
          }

      }

      ~TableBuilder() {
          if (!closed_) {
              Finish();
          }
          if (filter_block_) {
              filter_block_.reset();
          }
      }

      void Add(const Slice &key, const Slice &value);

      void Add(const Slice &key, const Slice &value, VLogEntryInfo *info);

      void Sync() {
          status_ = file_->Sync();
      }

      void Flush(bool next_index_entry = true);

      bool OK() const { return status_.ok(); }

      Status status() const { return status_; }

      Status Finish();

      uint64_t FileSize() const {
          return offset_;
      }

      uint64_t ApproximateFileSize() const {
          return offset_ + index_block_.BlockSize() + data_block_.BlockSize() + Option::kBlockTrailerSize +
                 (filter_block_ ? filter_block_->BlockSize() : 0);
      }

      uint64_t NumEntries() const {
          return num_entries_;
      }

  private:
      WritableFile *file_;

      bool need_next_index_entry_{false};

      BlockBuilder index_block_{};
      BlockBuilder data_block_{};

      BlockEntryInfo data_block_entry_{0, 0};


      size_t num_entries_{0};

      Status status_;

      bool closed_{false};

      uint64_t offset_{0};

      std::shared_ptr<Executor> executor_{};

      std::string last_key_;

      std::unique_ptr<FilterBlockBuilder> filter_block_{};
      const Comparator *comparator_;
  };

}// namespace LSMKV

#endif//BUILDER_H
