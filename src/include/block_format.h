#pragma once

#include <unordered_map>
#include <memory>
#include <utils/log.h>
#include <utils/lru.h>

#include "crc32c/crc32c.h"
#include "lsmkv/dbformat.h"
#include "utils/iterator.h"
#include "utils/filename.h"
#include "file.h"
#include "utils/bloomfilter.h"
#include "utils/utils.h"
#include "utils/executor.h"
#include "version.h"


namespace LSMKV {
  template<typename T>
  static inline T Max(const T &a, const T &b, Comparator *cmp) {
      return cmp->compare(a, b) > 0 ? a : b;
  }

  template<typename T>
  static inline T Min(const T &a, const T &b, Comparator *cmp) {
      return cmp->compare(a, b) < 0 ? a : b;
  }


  struct SSTFileMeta {
      bool InRange(const QueryKey &ikey) const {
          auto user_key = ikey.user_key();

          return StrComparator::compare_impl(user_key, largest.user_key()) <= 0 &&
                 StrComparator::compare_impl(user_key, smallest.user_key()) >= 0;
      }

      bool InRange(const InternalKey &small, const InternalKey &large, Comparator *cmp) const {
          return cmp->compare(Max(smallest.Encode(), small.Encode(), cmp),
                              Min(largest.Encode(), large.Encode(), cmp)) <= 0;
      }

      bool IsValid() const {
          return file_size_ > 0;
      }

      Iterator *NewIterator(const std::string &db_name, Comparator *cmp) const;

      SSTFileMeta() = default;

      SSTFileMeta(SSTFileMeta &&other) noexcept = default;

      SSTFileMeta &operator=(SSTFileMeta &&other) noexcept = default;

      Slice Encode() const {
          std::string dst;
          dst.resize(8);
          EncodeFixed32(&dst[0], smallest.size());
          EncodeFixed32(&dst[4], largest.size());
          auto key_sz = smallest.size() + largest.size();

          auto sum_size = key_sz + 2 * sizeof(uint64_t) + sizeof(uint32_t) * 2 + 8;

          dst.reserve(sum_size);
          dst.append(smallest.Encode());
          dst.append(largest.Encode());
          dst.resize(sum_size);

          auto buf = dst.data() + key_sz + 8;

          EncodeFixed64(buf, file_number_);
          EncodeFixed64(buf + 8, file_size_);
          EncodeFixed32(buf + 16, level_);
          EncodeFixed32(buf + 20, vlog_file_no_);

          return dst;
      }

      Status Decode(Slice src) {
          if (src.size() < 2 * sizeof(uint64_t) + sizeof(uint32_t) * 2 + 8) {
              return Status::Corruption("bad sst file meta");
          }

          auto smallest_sz = DecodeFixed32(src.data());
          auto largest_sz = DecodeFixed32(src.data() + 4);

          if (src.size() != smallest_sz + largest_sz + 2 * sizeof(uint64_t) + sizeof(uint32_t) * 2 + 8) {
              return Status::Corruption("bad sst file meta");
          }

          src.remove_prefix(8);

          smallest.Decode(Slice(src.data(), smallest_sz));

          src.remove_prefix(smallest_sz);

          largest.Decode(Slice(src.data(), largest_sz));

          src.remove_prefix(largest_sz);

          auto buf = src.data();

          file_number_ = DecodeFixed64(buf);
          file_size_ = DecodeFixed64(buf + 8);
          level_ = DecodeFixed32(buf + 16);
          vlog_file_no_ = DecodeFixed32(buf + 20);

          return Status::OK();
      }


      uint64_t file_size_{};
      uint64_t file_number_{};
      uint32_t level_{};
      uint32_t vlog_file_no_{};

      InternalKey smallest{};
      InternalKey largest{};
  };

  struct BlockEntryInfo {
      BlockEntryInfo() = default;

      BlockEntryInfo(uint64_t offset, uint64_t size) : offset_(offset), size_(size) {
          static_assert(sizeof(BlockEntryInfo) == 16 && std::is_standard_layout_v<BlockEntryInfo> &&
                        std::is_trivial_v<BlockEntryInfo>);
      }

      [[nodiscard]] std::string Encode() const {
          return {reinterpret_cast<const char *>(this), sizeof(BlockEntryInfo)};
      }

      Status Decode(Slice src) {
          assert(src.size() >= sizeof(BlockEntryInfo));
          auto ret = Status::OK();
          memcpy(this, src.data(), sizeof(BlockEntryInfo));
          return ret;
      }

      uint64_t offset_;
      uint64_t size_;
  };


  static inline Status ParserBlock(Slice *block, Slice *key) {
      if (block->empty()) {
          return Status::Corruption("bad block entry " + line_info());
      }
      auto sz = block->size();
      auto p = block->data();
      auto key_size = DecodeFixed32(p);


      auto value_size = DecodeFixed32(p + 4);
      if (sz < key_size + value_size + 8) {
          return Status::Corruption("bad block entry " + line_info());
      }
      *key = {block->data(), key_size + value_size + 8};

      block->remove_prefix(key_size + value_size + 8);

      return Status::OK();
  }


  class BlockIterator : public Iterator {
  private:
      void SetInValid() {
          cur_ = 1;
          end_ = 0;
      }

  public:
      BlockIterator() = default;

      BlockIterator(std::string s, Comparator *cmp) : block_buffer_(std::move(s)), userkey_cmp_(cmp) {
          seekToFirst();
      }

      BlockIterator(const std::string &file_name, const BlockEntryInfo &info, Comparator *cmp) : userkey_cmp_(cmp) {
          RandomReadableFile *file;
          auto status = NewRandomReadableFile(file_name, &file);
          if (!status.ok()) {
              return;
          }
          Slice input;
          block_buffer_.resize(info.size_);
          status = file->Read(info.offset_, info.size_, &input, block_buffer_.data());
          if (!status.ok()) {
              return;
          }

          seekToFirst();
      }

      BlockIterator(RandomReadableFile *file, const BlockEntryInfo &info, Comparator *cmp) : userkey_cmp_(cmp) {
          Slice input;
          block_buffer_.resize(info.size_);
          auto status = file->Read(info.offset_, info.size_, &input, block_buffer_.data());
          if (!status.ok()) {
              return;
          }

          seekToFirst();
      }

      BlockIterator(const BlockIterator &other) {
          if (this != &other) {
              block_buffer_ = other.block_buffer_;
              userkey_cmp_ = other.userkey_cmp_;
              cur_ = other.cur_;
              end_ = other.end_;
          }
      }

      BlockIterator &operator=(const BlockIterator &other) {
          if (this != &other) {
              block_buffer_ = other.block_buffer_;
              userkey_cmp_ = other.userkey_cmp_;
              cur_ = other.cur_;
              end_ = other.end_;
          }
          return *this;
      }

      BlockIterator(BlockIterator &&other) noexcept {
          if (this != &other) {
              block_buffer_ = std::move(other.block_buffer_);
              userkey_cmp_ = other.userkey_cmp_;
              cur_ = other.cur_;
              end_ = other.end_;
          }
      }

      BlockIterator &operator=(BlockIterator &&other) noexcept {
          if (this != &other) {
              block_buffer_ = std::move(other.block_buffer_);
              userkey_cmp_ = other.userkey_cmp_;
              cur_ = other.cur_;
              end_ = other.end_;
          }
          return *this;
      }

      ~BlockIterator() override = default;

      bool valid() const override {
          return cur_ < end_;
      }

      void reset() {
          cur_ = 0;
          end_ = 0;

          block_buffer_.clear();
      }

      void seekToFirst() override {
          cur_ = 0;
          end_ = block_buffer_.size();
      }

      void seek(const Slice &target) override {
          Slice input = {block_buffer_.data(), block_buffer_.size()};
          while (!input.empty()) {
              Slice block_entry;
              auto status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  SetInValid();
                  return;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  SetInValid();
                  return;
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              if (userkey_cmp_->compare(target, Slice(block_entry.data() + 8, key_size)) <= 0) {
                  cur_ = block_entry.data() - block_buffer_.data();
                  end_ = block_buffer_.size();
                  return;
              }
          }
          SetInValid();
      }

      void next() override {
          assert(valid());
          Slice input = {block_buffer_.data() + cur_, block_buffer_.size() - cur_};

          Slice block_entry;
          auto status = ParserBlock(&input, &block_entry);

          if (!status.ok()) {
              SetInValid();
              return;
          }
          cur_ += block_entry.size();
      }

      void AppendNext() {
          if (cur_ <= end_ && end_ != block_buffer_.size()) {
              Slice input = {block_buffer_.data() + end_, block_buffer_.size() - end_};
              Slice block_entry;
              auto status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  SetInValid();
                  return;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  SetInValid();
                  return;
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              end_ += block_entry.size();
          }
      }

      void seek(const Slice &K1,
                const Slice &K2) override {
          Slice input = {block_buffer_.data(), block_buffer_.size()};
          cur_ = block_buffer_.size();
          while (!input.empty()) {
              Slice block_entry;
              auto status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  SetInValid();
                  return;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  SetInValid();
                  return;
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              if (userkey_cmp_->compare(K1, Slice(block_entry.data() + 8, key_size)) <= 0) {
                  cur_ = block_entry.data() - block_buffer_.data();
                  break;
              }
          }

          if (cur_ == block_buffer_.size()) {
              SetInValid();
              return;
          }

          input = {block_buffer_.data() + cur_, block_buffer_.size() - cur_};

          while (!input.empty()) {
              Slice block_entry;
              auto status = ParserBlock(&input, &block_entry);

              if (!status.ok()) {
                  SetInValid();
                  return;
              }

              if (block_entry.empty() || block_entry.size() < 8) {
                  SetInValid();
                  return;
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              if (userkey_cmp_->compare(K2, Slice(block_entry.data() + 8, key_size)) < 0) {
                  end_ = block_entry.data() - block_buffer_.data();
                  return;
              }
          }
          end_ = block_buffer_.size();
      }

      Slice key() const override {
          return {block_buffer_.data() + cur_ + 8, DecodeFixed32(block_buffer_.data() + cur_)};
      }

      Slice value() const override {
          return {block_buffer_.data() + cur_ + 8 + DecodeFixed32(block_buffer_.data() + cur_),
                  DecodeFixed32(block_buffer_.data() + cur_ + 4)};
      }

  private:
      uint32_t cur_{};
      uint32_t end_{};

      // I know this will ignore sequence number
      // but for seek(k1, k2) it must use userkey_cmp_ in k2 seek
      Comparator *userkey_cmp_{};

      std::string block_buffer_{};
  };

  enum {
      kTableMagicNumber = 0xdb4775248b80fb57ull
  };

  class Footer {
  public:
      enum {
          kBlockInfoLength = sizeof(BlockEntryInfo) * 3,
          kEncodedLength = kBlockInfoLength + 8
      };

      Footer() = default;

      // The block handle for the metaindex block of the table
      const BlockEntryInfo &metaindex_handle() const { return metaindex_handle_; }

      void set_metaindex_handle(const BlockEntryInfo &h) { metaindex_handle_ = h; }

      // The block handle for the index block of the table
      const BlockEntryInfo &index_handle() const { return index_handle_; }

      void set_index_handle(const BlockEntryInfo &h) { index_handle_ = h; }

      const BlockEntryInfo &model_handle() const { return model_handle_; }

      void set_model_handle(const BlockEntryInfo &h) { model_handle_ = h; }

      [[nodiscard]] std::string Encode() const {
          std::string dst;
          dst.reserve(kEncodedLength);
          dst.append(metaindex_handle_.Encode());
          dst.append(index_handle_.Encode());
          dst.append(model_handle_.Encode());

          dst.append(kEncodedLength - kBlockInfoLength, '\0');

          auto buf = dst.data() + kBlockInfoLength;

          EncodeFixed32(buf, kTableMagicNumber & 0xfffffffu);
          EncodeFixed32(buf + 4, kTableMagicNumber >> 32);

          return dst;
      }

      Status Decode(Slice input) {
          auto status = metaindex_handle_.Decode(input);

          if (status.ok()) {
              input.remove_prefix(sizeof(BlockEntryInfo));
              status = index_handle_.Decode(input);
          }

          if (status.ok()) {
              input.remove_prefix(sizeof(BlockEntryInfo));
              status = model_handle_.Decode(input);
          }

          if (status.ok()) {
              input.remove_prefix(sizeof(BlockEntryInfo));

              uint32_t magic_lo = DecodeFixed32(input.data());

              if (magic_lo != (kTableMagicNumber & 0xfffffffu)) {
                  status = Status::Corruption("not an sstable (bad magic number)");
                  return status;
              }

              uint32_t magic_hi = DecodeFixed32(input.data() + 4);

              if (magic_hi != (kTableMagicNumber >> 32)) {
                  status = Status::Corruption("not an sstable (bad magic number)");
                  return status;
              }
          }

          return status;
      }

  private:
      BlockEntryInfo model_handle_;
      BlockEntryInfo metaindex_handle_;
      BlockEntryInfo index_handle_;
  };

  struct VLogEntryInfo {
      VLogEntryInfo() = default;

      VLogEntryInfo(uint32_t file_no, uint32_t length, uint64_t offset)
              : offset_(offset), file_no_(file_no), length_(length) {
          static_assert(sizeof(VLogEntryInfo) == 16 && std::is_standard_layout_v<VLogEntryInfo> &&
                        std::is_trivial_v<VLogEntryInfo>);
      }

      VLogEntryInfo &operator=(const VLogEntryInfo &) = default;

      VLogEntryInfo(const VLogEntryInfo &) = default;

      VLogEntryInfo &operator=(VLogEntryInfo &&) = default;

      VLogEntryInfo(VLogEntryInfo &&) = default;

      ~VLogEntryInfo() = default;

      Status Decode(Slice src) {
          if (src.size() != sizeof(VLogEntryInfo)) {
              return Status::Corruption("bad vlog entry info");
          }
          memcpy(this, src.data(), sizeof(VLogEntryInfo));
          return Status::OK();
      }

      [[nodiscard]] std::string Encode() const {
          return {reinterpret_cast<const char *>(this), sizeof(VLogEntryInfo)};
      }

      Slice ToSlice() const {
          return {reinterpret_cast<const char *>(this), sizeof(VLogEntryInfo)};
      }


      // put file_no_ after offset_ make it a pod
      // to avoid 4 bytes padding

      // vlog offset
      uint64_t offset_;
      // vlog file number
      uint32_t file_no_;
      // vlog length
      uint32_t length_;
  };


  // must call seek(or seekFirst) before use
  class TableIterator : public Iterator {
  private:
      Status NextBlock() {
          assert(index_iter_.valid());
          Status status = Status::OK();

          auto value = index_iter_.value();
          BlockEntryInfo b_info{};
          status = b_info.Decode(value);

          if (!status.ok()) {
              index_iter_.reset();
              return status;
          }

          block_iter_ = BlockIterator(file_, b_info, cmp_);

          return status;
      }

  public:
      TableIterator(const std::string &db_name, const SSTFileMeta *meta, Comparator *cmp) : db_name_(
              db_name), cmp_(cmp) {
          auto status = NewRandomReadableFile(SSTFileName(db_name_, meta->file_number_), &file_);
          if (!status.ok()) {
              return;
          }

          Slice input;
          std::string buffer;
          buffer.resize(Footer::kEncodedLength);

          status = file_->Read(meta->file_size_ - Footer::kEncodedLength, Footer::kEncodedLength, &input,
                               buffer.data());

          if (!status.ok()) {
              return;
          }

          Footer footer{};

          status = footer.Decode(input);

          if (!status.ok()) {
              return;
          }

          const auto &index_handle = footer.index_handle();
          index_iter_ = BlockIterator(file_, index_handle, cmp);
          block_iter_ = BlockIterator();
      }

      TableIterator(const TableIterator &) = delete;

      TableIterator &operator=(const TableIterator &) = delete;

      ~TableIterator() override = default;

      bool valid() const override {
          return index_iter_.valid() || block_iter_.valid();
      }

      void seekToFirst() override {
          index_iter_.seekToFirst();
          if (!index_iter_.valid()) {
              return;
          }

          NextBlock();

          if (index_iter_.valid()) {
              index_iter_.next();
          }
      }

      void seek(const Slice &target) override {
          index_iter_.seek(target);
          block_iter_.reset();

          if (!index_iter_.valid()) {
              return;
          }

          NextBlock();

          block_iter_.seek(target);
      }

      void next() override {
          assert(valid());

          if (block_iter_.valid()) {
              block_iter_.next();
          }

          if (block_iter_.valid() || !index_iter_.valid()) {
              return;
          }


          block_iter_.reset();
          NextBlock();

          if (index_iter_.valid()) {
              index_iter_.next();
          }

      }

      void seek(const Slice &K1,
                const Slice &K2) override {
          index_iter_.seek(K1, K2);
          block_iter_.reset();

          std::string continuous_block;
          if (index_iter_.valid()) {
              index_iter_.AppendNext();
              do {
                  auto value = index_iter_.value();
                  BlockEntryInfo b_info{};
                  auto status = b_info.Decode(value);

                  if (!status.ok()) {
                      index_iter_.reset();
                      return;
                  }

                  auto sz = continuous_block.size();

                  continuous_block.append(b_info.size_, '\0');

                  Slice input;
                  status = file_->Read(b_info.offset_, b_info.size_, &input,
                                       continuous_block.data() + sz);
                  if (!status.ok()) {
                      index_iter_.reset();
                      return;
                  }
                  index_iter_.next();
              } while (index_iter_.valid());
          } else {
              // because index_block maintain the last_key of the block,
              // so we need to seek to the first block
              index_iter_.seekToFirst();

              if (!index_iter_.valid()) {
                  return;
              }
              auto value = index_iter_.value();
              BlockEntryInfo b_info{};
              auto status = b_info.Decode(value);
              if (!status.ok()) {
                  index_iter_.reset();
                  return;
              }

              auto sz = continuous_block.size();

              continuous_block.append(b_info.size_, '\0');

              Slice input;
              status = file_->Read(b_info.offset_, b_info.size_, &input,
                                   continuous_block.data() + sz);
              if (!status.ok()) {
                  index_iter_.reset();
                  return;
              }

              index_iter_.reset();
          }


          block_iter_ = BlockIterator(std::move(continuous_block), cmp_);
          block_iter_.seek(K1, K2);
      }

      Slice key() const override {
          assert(valid());

          assert(block_iter_.valid());
          return block_iter_.key();
      }

      Slice value() const override {
          assert(valid());

          assert(block_iter_.valid());
          return block_iter_.value();
      }

  private:
      RandomReadableFile *file_{};

      BlockIterator block_iter_{};

      BlockIterator index_iter_{};
      Comparator *cmp_{};

      const std::string &db_name_;
  };


} // namespace LSMKV