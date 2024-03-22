#ifndef VLOGBUILDER_H
#define VLOGBUILDER_H

#include "utils/coding.h"
#include "utils/executor.h"
#include "utils/slice.h"
#include "utils/utils.h"
#include <string>

namespace LSMKV {
class VLogBuilder {
private:
  // Header: magic(1) + crc(2) + key_size(4) + value_size(4)
  static constexpr size_t kHeaderSize = 11;

  static constexpr const char magic = '\377';

public:
  struct VLogInfo {
    VLogInfo() = delete;
    VLogInfo(uint32_t file_no, uint32_t length, uint64_t offset)
        : offset_(offset), file_no_(file_no), length_(length) {}

    VLogInfo &operator=(const VLogInfo &) = default;
    VLogInfo(const VLogInfo &) = default;
    VLogInfo &operator=(VLogInfo &&) = default;
    VLogInfo(VLogInfo &&) = default;
    ~VLogInfo() = default;

    uint64_t offset_;
    uint32_t file_no_;
    uint32_t length_;
  };

  explicit VLogBuilder(WritableFile *file) : file_(file){};

  explicit VLogBuilder(const std::string &fname) {
    auto status = NewAppendableFile(fname, &file_);
    // TODO: handle error
    assert(status.ok());
  }

  // Header: magic(1) + crc(2) + key_size(4) + value_size(4)
  VLogInfo Append(const Slice &key, const Slice &value) {
    auto size = kHeaderSize + value.size() + key.size();
    auto buf = file_->WriteToBuffer(size <= 32768 ? size : kHeaderSize);
    buf[0] = magic;
    EncodeFixed32(buf + 3, key.size());
    EncodeFixed32(buf + 7, value.size());

    memcpy(buf + kHeaderSize, key.data(), key.size());

    std::future<uint16_t> crc =
        default_scheduler().submit([key_sz = key.size(), buf, val = value] {
          return utils::crc16_with_prefix(buf + 3, 8 + key_sz, val.data(),
                                          val.size());
        });

    EncodeFixed16(buf + 1, crc.get());

    uint64_t offset = offset_;

    offset_ += size;

    if (size <= 32768) [[likely]] {
      memcpy(buf + kHeaderSize + key.size(), value.data(), value.size());
      return {file_no_, size, offset};
    }
    // for large value, write directly to disk
    file_->Flush();
    (file_->WriteUnbuffered(value.data(), value.size()));
    return {file_no_, size, offset};
  }

  void Drop() {
    file_->Close();
    delete file_;
  }

private:
  // magic 0xff

  uint32_t file_no_ = 0;

  uint64_t offset_ = 0;

  WritableFile *file_;
};
} // namespace LSMKV
#endif // VLOGBUILDER_H
