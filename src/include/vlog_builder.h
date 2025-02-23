#ifndef VLOGBUILDER_H
#define VLOGBUILDER_H

#include <string>

#include "utils/coding.h"
#include "utils/executor.h"
#include "utils/slice.h"
#include "utils/utils.h"
#include "format.h"

namespace LSMKV {
  class VLogBuilder {
  private:
      // Header: magic(6) + crc(2) + key_size(4) + value_size(4) + sequence & type (8)
      static constexpr size_t kHeaderSize = 24;

      static constexpr const char magic = '\377';

  public:
      explicit VLogBuilder(const std::string &db_name) : db_name_(db_name) {};

      Status Open(uint32_t file_no) {
          file_no_ = file_no;
          offset_ = 0;
          auto status = NewAppendableFile(VLogFileName(db_name_, file_no), &file_);
          if (!status.ok()) {
              return status;
          }
          return Status::OK();
      }

      void reset() {
          if (file_ != nullptr) {
              file_->Close();
              delete file_;
          }
          offset_ = 0;
          file_no_ = 0;
          file_ = nullptr;
      }

      Status Close() {
          Status s = Status::OK();
          if (file_ != nullptr) {
              s = file_->Flush();
              s = file_->Close();
              delete file_;
              file_ = nullptr;
          }
          offset_ = 0;
          return s;
      }

      ~VLogBuilder() {
          if (file_ != nullptr) {
              file_->Close();
              delete file_;
          }
      }

      bool Full(uint32_t value_sz) const {
          return offset_ >= Option::kMaxVLogSize ||
                 (offset_ > Option::kMaxVLogSize / 2 && value_sz > Option::kMaxVLogSize / 2);
      }

      // Header: magic(6) + crc(2) + key_size(4) + value_size(4) + sequence & type (8)
      Status Append(SequenceNumber seq, const Slice &key, const Slice &value, VLogEntryInfo *info, bool sync = false) {
          Status s;

          // TODO: replace with real size
          if (value.size() > 30000) {
              auto size = kHeaderSize + key.size();
              assert(size < 65536);
              auto buf = file_->WriteToBuffer(size);

                buf[0] = magic;
                buf[1] = magic;
                EncodeFixed32(buf + 2, 0xa8fa88d7);
                // leave space for crc(2)
                EncodeFixed32(buf + 8, key.size());
                EncodeFixed32(buf + 12, value.size());
                EncodeFixed64(buf + 16, seq);
                memcpy(buf + kHeaderSize, key.data(), key.size());
                auto crc = utils::crc16_with_prefix(buf + 8, size - 8, value.data(), value.size());
                EncodeFixed16(buf + 6, crc);
                s = file_->Flush();

                if (!s.ok()) {
                    return s;
                }

                s = file_->Append(value);

                if (!s.ok()) {
                    return s;
                }

                s = file_->Flush();

                if (!s.ok()) {
                    return s;
                }

                *info = {file_no_, size + value.size(), offset_};

                offset_ += size + value.size();
          } else {

              auto size = kHeaderSize + value.size() + key.size();

              auto buf = file_->WriteToBuffer(size);
              buf[0] = magic;
              buf[1] = magic;
              EncodeFixed32(buf + 2, 0xa8fa88d7);

              // leave space for crc(2)

              EncodeFixed32(buf + 8, key.size());
              EncodeFixed32(buf + 12, value.size());
              EncodeFixed64(buf + 16, seq);

              memcpy(buf + kHeaderSize, key.data(), key.size());

              memcpy(buf + kHeaderSize + key.size(), value.data(), value.size());

              auto crc = utils::crc16(buf + 8, size - 8);

              EncodeFixed16(buf + 6, crc);

              uint64_t offset = offset_;

              offset_ += size;


              s = file_->Flush();

              if (!s.ok()) {
                  return s;
              }

              *info = {file_no_, size, offset};
          }


          return Status::OK();

      }

  private:
      // magic 0xff

      uint32_t file_no_ = 0;

      uint64_t offset_ = 0;

      const std::string &db_name_;
      WritableFile *file_;
  };
} // namespace LSMKV
#endif // VLOGBUILDER_H
