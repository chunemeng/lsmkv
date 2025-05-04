#include "include/reader.h"
#include "include/model.h"

namespace LSMKV {

  Status SSTReader::ReadOne(const LSMKV::SSTFileMeta *meta, LSMKV::Slice internal_key,
                            LSMKV::VLogEntryInfo *vlog_info) {
      std::unique_ptr<SequentialFile> file;
      Status status = NewSequentialFile(SSTFileName(db_name_, meta->file_number_), &file);
      if (!status.ok()) {
          return status;
      }

      Footer footer{};

      Slice input;
      std::string buffer;
      buffer.resize(Footer::kEncodedLength);

      status = file->MoveTo(meta->file_size_ - Footer::kEncodedLength);

      if (!status.ok()) {
          return status;
      }

      file->Read(Footer::kEncodedLength, &input, buffer.data());

      status = footer.Decode(input);

      if (!status.ok()) {
          return status;
      }

      const auto &metaindex_handle = footer.metaindex_handle();
      const auto &index_handle = footer.index_handle();
      const auto &model_handle = footer.model_handle();

      buffer.resize(metaindex_handle.size_);

      status = file->MoveTo(metaindex_handle.offset_);

      if (!status.ok()) {
          return status;
      }

      file->Read(metaindex_handle.size_, &input, buffer.data());
      if (!status.ok()) {
          return status;
      }
      if (!KeyMayMatch(ExtractUserKey(internal_key), input)) {
          return Status::NotFound(line_info());
      }

      // use linear model
      if (model_handle.size_ != 0) {
          buffer.resize(model_handle.size_);

          status = file->MoveTo(model_handle.offset_);

          if (!status.ok()) {
              return status;
          }

          status = file->Read(model_handle.size_, &input, buffer.data());

          if (!status.ok()) {
              return status;
          }

          std::pair<int64_t, int64_t> location{0, 0};
          status = FindByModel(input, internal_key, &location);
          if (!status.ok()) {
              return status;
          }

          auto read_size = location.second - location.first + Linear::blob_size;
          buffer.resize(read_size);

          status = file->MoveTo(location.first);
          if (!status.ok()) {
              return status;
          }
          status = file->Read(read_size, &input, buffer.data());
          if (!status.ok()) {
              return status;
          }
          return FindInBlock(input, internal_key, vlog_info);
      } else {
          buffer.resize(index_handle.size_);

          status = file->MoveTo(index_handle.offset_);

          if (!status.ok()) {
              return status;
          }

          file->Read(index_handle.size_, &input, buffer.data());

          if (!status.ok()) {
              return status;
          }

          BlockEntryInfo b_info{};

          status = FilterInIndex(input, internal_key, &b_info);

          if (!status.ok()) {
              return status;
          }

          buffer.resize(b_info.size_);

          status = file->MoveTo(b_info.offset_);

          if (!status.ok()) {
              return status;
          }

          file->Read(b_info.size_, &input, buffer.data());

          if (!status.ok()) {
              return status;
          }

          return FindInBlock(input, internal_key, vlog_info);
      }
  }

  Status SSTReader::FindByModel(Slice input, const Slice &internal_key, std::pair<int64_t, int64_t> *location) {
      Comparator user_cmp = StrComparator();
      Status status = Status::OK();
      Slice key;
      Slice linears;
      auto user_key = DecodeFixed64BigEnd(internal_key.data());
      if (user_key == 2) {
          int a = 5;
      }

      while (!input.empty()) {
          if (input.size() < Linear::model_block_size) {
              return Status::Corruption("invalid model block " + line_info());
          }

          key = Slice(input.data(), 8);
          auto size = DecodeFixed32(input.data() + 8);
          int64_t start = DecodeFixed32(input.data() + 12);
          int64_t end = DecodeFixed32(input.data() + 16);

          if (input.size() < size * Linear::linear_params_size + Linear::model_block_size) {
              return Status::Corruption("invalid model block " + line_info());
          }

          linears = Slice(input.data() + Linear::model_block_size, size * Linear::linear_params_size);

          auto ret = user_cmp.compare(ExtractUserKey(internal_key), key);
          assert((end - start) % 40 == 0);

          if (ret <= 0) {
              location->first = std::numeric_limits<uint32_t>::max();
              location->second = 0;
              auto &loc = *location;
              for (auto i = 0; i < size; ++i) {
                  auto linear = Slice(linears.data() + i * Linear::linear_params_size, Linear::linear_params_size);
                  Linear l(linear);
                  auto predict = l.Predict(user_key);
                  int32_t off = 2 * Linear::blob_size;
                  loc.first = std::min(loc.first, static_cast<int64_t>(predict - off));
                  loc.second = std::max(loc.second, static_cast<int64_t>(predict + off));
              }
              loc.first = std::max(std::max(loc.first, start), 0L);
              {
                  auto pd = loc.first - start;
                  auto mod = pd % 40;
                  loc.first -= mod;
              }

              loc.second = std::max(std::max(std::min(loc.second, end), 0L), loc.first);

              {
                  auto pd = loc.second - start;
                  auto mod = pd % 40;
                  loc.second += 40 - mod;
              }

              return Status::OK();
          } else {
              input.remove_prefix(size * Linear::linear_params_size + Linear::model_block_size);
          }
      }
      return Status::NotFound("not found key in FindInBlock " + line_info());
  }
} // namespace LSMKV