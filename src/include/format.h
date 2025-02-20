#pragma once

#include "lsmkv/dbformat.h"
#include "utils/iterator.h"
#include "utils/filename.h"
#include "utils/file.h"
#include "utils/bloomfilter.h"
#include "utils/utils.h"


namespace LSMKV {

  class TableIterator : public Iterator {
  public:
      TableIterator() = default;

      TableIterator(const TableIterator &) = delete;

      TableIterator &operator=(const TableIterator &) = delete;

      ~TableIterator() = default;

      bool hasNext() const override;

      void seekToFirst() override;

      void seek(const Slice &target) override;

      void next() override;

      void seek(const Slice &K1,
                const Slice &K2) override;

      Slice key() const override;

      Slice value() const override;

  private:
      std::string buffer_;
      const std::string &db_name_;
  };


  struct SSTFileMeta {
      bool InRange(const QueryKey &ikey) const {
          auto user_key = ikey.user_key();

          return StrComparator::compare_impl(user_key, largest.user_key()) <= 0 &&
                 StrComparator::compare_impl(user_key, smallest.user_key()) >= 0;
      }


      uint64_t file_size_{};
      uint64_t file_number_{};
      uint32_t level_{};

      InternalKey smallest{};
      InternalKey largest{};
  };

  struct BlockEntryInfo {
      BlockEntryInfo() = default;

      BlockEntryInfo(uint64_t offset, uint64_t size) : offset_(offset), size_(size) {
          static_assert(sizeof(BlockEntryInfo) == 16 && std::is_pod_v<BlockEntryInfo>);
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

  enum {
      kTableMagicNumber = 0xdb4775248b80fb57ull
  };

  class Footer {
  public:
      enum { kEncodedLength = 2 * sizeof(BlockEntryInfo) + 8 };

      Footer() = default;

      // The block handle for the metaindex block of the table
      const BlockEntryInfo &metaindex_handle() const { return metaindex_handle_; }

      void set_metaindex_handle(const BlockEntryInfo &h) { metaindex_handle_ = h; }

      // The block handle for the index block of the table
      const BlockEntryInfo &index_handle() const { return index_handle_; }

      void set_index_handle(const BlockEntryInfo &h) { index_handle_ = h; }

      [[nodiscard]] std::string Encode() const {
          std::string dst;
          dst.reserve(kEncodedLength);
          dst.append(metaindex_handle_.Encode());
          dst.append(index_handle_.Encode());

          dst.append(8, '\0');
          EncodeFixed32(dst.data() + 2 * sizeof(BlockEntryInfo), kTableMagicNumber & 0xfffffffu);
          EncodeFixed32(dst.data() + 2 * sizeof(BlockEntryInfo) + 4, kTableMagicNumber >> 32);

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
      BlockEntryInfo metaindex_handle_;
      BlockEntryInfo index_handle_;
  };

  struct VLogEntryInfo {
      VLogEntryInfo() = default;

      VLogEntryInfo(uint32_t file_no, uint32_t length, uint64_t offset)
              : offset_(offset), file_no_(file_no), length_(length) {
          static_assert(sizeof(VLogEntryInfo) == 16 && std::is_pod_v<VLogEntryInfo>);
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

      // put file_no_ after offset_ make it a pod
      // to avoid 4 bytes padding

      // vlog offset
      uint64_t offset_;
      // vlog file number
      uint32_t file_no_;
      // vlog length
      uint32_t length_;
  };

  class VLogReader {
  private:
      Status InitFile(uint32_t file_no) {
          std::string fname = VLogFileName(db_name_, file_no);
          auto status = NewRandomReadableFile(fname, &file_);
          if (!status.ok()) {
              return status;
          }

          return Status::OK();
      }

  public:
      explicit VLogReader(const std::string &db_name)
              : db_name_(db_name) {
      }

      void Reset() {
          delete file_;
          file_ = nullptr;
      }

      ~VLogReader() {
          delete file_;
      }


      Status Read(const VLogEntryInfo &info, std::string *value) {
          auto status = InitFile(info.file_no_);
          if (!status.ok()) {
              return status;
          }

          std::string tmp;
          tmp.resize(info.length_);

          Slice result_slice;
          char *buf = tmp.data();

          file_->Read(info.offset_, info.length_, &result_slice, buf);
          if (buf[0] != '\377' || buf[1] != '\377') {
              return Status::Corruption("bad magic number");
          }
          uint32_t magic = DecodeFixed32(buf + 2);

          if (magic != 0xa8fa88d7) {
              return Status::Corruption("bad magic number");
          }

          uint16_t crc = DecodeFixed16(buf + 6);

          uint32_t key_size = DecodeFixed32(buf + 8);
          uint32_t value_size = DecodeFixed32(buf + 12);

          if (crc != utils::crc16(buf + 8, info.length_ - 8)) {
              return Status::Corruption("crc error");
          }

          uint64_t seq = DecodeFixed64(buf + 16);

          value->clear();
          value->append(buf + 24 + key_size, value_size);

          return Status::OK();
      }


  private:
      RandomReadableFile *file_{nullptr};
      const std::string db_name_;
  };

  class SSTReader {
  private:
      Status ParserBlock(Slice &block, Slice *key) {
          if (block.empty()) {
              return Status::Corruption("bad block entry");
          }


          auto sz = block.size();
          auto p = block.data();
          auto key_size = DecodeFixed32(p);
          auto value_size = DecodeFixed8(p + 4);

          if (sz < key_size + value_size + 5) {
              return Status::Corruption("bad block entry");
          }

          *key = {block.data(), key_size + value_size + 5};

          block.remove_prefix(key_size + value_size + 5);

          return Status::OK();
      }

  public:

      SSTReader(const std::string &db_name) : db_name_(db_name) {
      }

      Status ReadOne(const SSTFileMeta *meta, Slice internal_key, VLogEntryInfo *vlog_info) {
          RandomReadableFile *file;
          Status status = NewRandomReadableFile(SSTFileName(db_name_, meta->file_number_), &file);
          if (!status.ok()) {
              return status;
          }

          Footer footer{};

          Slice input;
          std::string buffer;
          buffer.resize(Footer::kEncodedLength);

          file->Read(meta->file_size_ - Footer::kEncodedLength, Footer::kEncodedLength, &input, buffer.data());

          status = footer.Decode(input);

          if (!status.ok()) {
              return status;
          }

          const auto &metaindex_handle = footer.metaindex_handle();
          const auto &index_handle = footer.index_handle();

          buffer.resize(metaindex_handle.size_);
          file->Read(metaindex_handle.offset_, metaindex_handle.size_, &input, buffer.data());
          if (!status.ok()) {
              return status;
          }
          if (!KeyMayMatch(ExtractUserKey(internal_key), input)) {
              return Status::NotFound();
          }

          buffer.resize(index_handle.size_);

          file->Read(index_handle.offset_, index_handle.size_, &input, buffer.data());

          if (!status.ok()) {
              return status;
          }

          std::optional<BlockEntryInfo> block_entry_info = std::nullopt;

          Comparator *cmp = new InternalKeyComparator();
          Comparator *user_cmp = new StrComparator();

          while (!input.empty()) {
              Slice block_entry;
              status = ParserBlock(input, &block_entry);

              if (!status.ok()) {
                  return status;
              }

              if (block_entry.empty() || block_entry.size() < 4) {
                  return Status::Corruption("bad block entry");
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              if (cmp->compare(internal_key, Slice(block_entry.data() + 5, key_size)) <= 0) {
                  block_entry.remove_prefix(key_size + 5);
                  BlockEntryInfo b_info{};
                  status = b_info.Decode(block_entry);
                  if (!status.ok()) {
                      return status;
                  }
                  block_entry_info = b_info;
                  break;
              }
          }

          if (!block_entry_info.has_value()) {
              return Status::NotFound();
          }

          auto b_info = block_entry_info.value();

          buffer.resize(b_info.size_);
          file->Read(b_info.offset_, b_info.size_, &input, buffer.data());

          if (!status.ok()) {
              return status;
          }

          while (!input.empty()) {
              Slice block_entry;
              status = ParserBlock(input, &block_entry);

              if (!status.ok()) {
                  return status;
              }

              if (block_entry.empty() || block_entry.size() < 4) {
                  return Status::Corruption("bad block entry");
              }

              uint32_t key_size = DecodeFixed32(block_entry.data());

              auto block_internal_key = Slice(block_entry.data() + 5, key_size);

              auto ret = user_cmp->compare(ExtractUserKey(internal_key), ExtractUserKey(block_internal_key));

              if (ret < 0) {
                  return Status::NotFound();
              }

              if (ret == 0) {
                  auto seq = ExtractSequenceNumber(block_internal_key);
                  auto query_seq = ExtractSequenceNumber(internal_key);

                  if (seq <= query_seq) {
                      block_entry.remove_prefix(key_size + 5);
                      status = vlog_info->Decode(block_entry);
                      return status;
                  }

                  return Status::NotFound();
              }
          }
          return Status::NotFound();
      }

      Status
      ReadBatch(const std::vector<const SSTFileMeta *> &metas, Slice internal_key, VLogEntryInfo *vlog_info) {
          for (const auto &meta: metas) {
              auto status = ReadOne(meta, internal_key, vlog_info);
              if (status.ok() || !status.IsNotFound()) {
                  return status;
              }
          }
          return Status::NotFound();
      }

  private:
      const std::string db_name_;
  };

} // namespace LSMKV