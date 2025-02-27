#ifndef VLOGBUILDER_H
#define VLOGBUILDER_H

#include <string>

#include "utils/coding.h"
#include "utils/executor.h"
#include "utils/slice.h"
#include "utils/utils.h"
#include "block_format.h"

namespace LSMKV {
  class VLogBuilder {
  private:
      static std::string EncodeKey(const Slice &key, SequenceNumber seq) {
          std::string result;
          result.reserve(key.size() + 8);
          result.append(key.data(), key.size());
          result.resize(key.size() + 8);
          EncodeFixed64(result.data() + key.size(), seq);
          return result;
      }

  public:
      explicit VLogBuilder(const std::string &db_name, std::shared_ptr<Executor> executor) : db_name_(db_name),
                                                                                             comparator_(
                                                                                                     InternalKeyComparator()),
                                                                                             executor_(std::move(
                                                                                                     executor)) {

      };

      uint32_t file_number() const {
          return file_no_;
      }

      Status Open(uint32_t file_no) {
          file_no_ = file_no;
          auto status = NewAppendableFile(VLogFileName(db_name_, file_no), &file_);
          if (!status.ok()) {
              return status;
          }

          builder_ = std::make_unique<TableBuilder>(file_.get(), &comparator_, executor_, false);

          return Status::OK();
      }

      void reset() {
          file_no_ = 0;
          builder_.reset();

          file_.reset();
      }

      Status Close() {
          Status s = Status::OK();
          builder_.reset();
          file_.reset();
          return s;
      }

      ~VLogBuilder() {
          Close();
      }

      bool Full(uint32_t value_sz) const {
          auto offset_ = builder_->ApproximateFileSize();
          return offset_ >= Option::kMaxVLogSize ||
                 (offset_ > Option::kMaxVLogSize / 2 && value_sz > Option::kMaxVLogSize / 2);
      }

      Status Append(SequenceNumber seq, const Slice &key, const Slice &value, VLogEntryInfo *info, bool sync = true) {
          std::string internal_key = EncodeKey(key, seq);
          return Append(internal_key, value, info, sync);
      }

      Status Append(const Slice &internal_key, const Slice &value, VLogEntryInfo *info, bool sync = true) {
          builder_->Add(internal_key, value, info);

          offset_ += info->length_;

          info->file_no_ = file_no_;

          if (builder_->OK()) {
              bool next_index = offset_ > Option::block_size;

              offset_ = next_index ? 0 : offset_;

              builder_->Flush(next_index);

              if (builder_->OK() && sync && false) {
                  builder_->Sync();
              }
          }

          return builder_->status();
      }

  private:
      uint32_t file_no_ = 0;

      uint64_t offset_ = 0;

      std::shared_ptr<Executor> executor_;

      std::unique_ptr<TableBuilder> builder_;

      Comparator comparator_;

      const std::string &db_name_;
      std::unique_ptr<WritableFile> file_;
  };
} // namespace LSMKV
#endif // VLOGBUILDER_H
