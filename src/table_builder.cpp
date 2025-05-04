#include "include/builder.h"

namespace LSMKV {

  void TableBuilder::Add(const Slice &key, const Slice &value, VLogEntryInfo *info) {
      assert(!closed_);

      if (!OK()) return;

      if (need_next_index_entry_) {
          index_block_.Add(last_key_, DataBlockEntryInfo());
          need_next_index_entry_ = false;
      }

      if (filter_block_ != nullptr) {
          filter_block_->AddKey(ExtractUserKey(key));
      }

      num_entries_++;

      info->offset_ = offset_ + data_block_.BlockSize();

      info->length_ = value.size() + key.size() + 8;

      data_block_.Add(key, value);

      const size_t block_size = data_block_.BlockSize();
      if (block_size >= Option::block_size) {
          last_key_ = key;
          Flush();
      }
  }


  void TableBuilder::Add(const Slice &key, const Slice &value) {
      assert(!closed_);

      if (!OK()) return;

      if (need_next_index_entry_) {
          index_block_.Add(last_key_, DataBlockEntryInfo());
          need_next_index_entry_ = false;
      }

      last_key_ = key;

      if (filter_block_ != nullptr) {
          filter_block_->AddKey(ExtractUserKey(key));
      }

      if (trainer_ != nullptr) {
          trainer_->Add(key, data_block_.BlockSize() + offset_);
      }

      num_entries_++;
      data_block_.Add(key, value);

      const size_t block_size = data_block_.BlockSize();
      if (block_size >= Option::block_size) {
          Flush();
      }
  }


  void TableBuilder::Flush(bool next_index_entry) {
      if (!OK()) return;

      if (filter_block_ != nullptr) {
          filter_block_->Flush();
      }

      if (trainer_ != nullptr) {
          trainer_->End();
      }

      if (data_block_.Empty()) return;

      WriteBlock(&data_block_, &data_block_entry_);
      if (OK()) {
          need_next_index_entry_ = next_index_entry;
          status_ = file_->Flush();
      }
  }

  Status TableBuilder::Finish() {
      closed_ = true;
      Flush();

      BlockEntryInfo filter_block_handle{}, index_block_handle{}, model_block_handle{};

      // Write filter block
      if (OK() && filter_block_ != nullptr) {
          WriteRawBlock(filter_block_->Finish(),
                        &filter_block_handle);
      }

      if (OK() && trainer_ != nullptr) {
          trainer_->Train();
          WriteRawBlock(trainer_->rep_, &model_block_handle);
      }

      // Write index block
      if (OK()) {
          if (need_next_index_entry_) {
              std::string handle_encoding;

              index_block_.Add(last_key_, DataBlockEntryInfo());

              need_next_index_entry_ = false;
          }
          WriteBlock(&index_block_, &index_block_handle);
      }

      // Write footer
      if (OK()) {
          Footer footer{};
          footer.set_metaindex_handle(filter_block_handle);
          footer.set_index_handle(index_block_handle);
          footer.set_model_handle(model_block_handle);
          std::string footer_encoding = footer.Encode();

          status_ = file_->Append(footer_encoding);
          if (status_.ok()) {
              offset_ += footer_encoding.size();
          }
      }
      return status_;
  }


} // namespace LSMKV