#ifndef OPTION_H
#define OPTION_H

namespace LSMKV {
  struct Option {
      Option() = default;

      ~Option() = default;

      static constexpr bool isFilter = true;

      static constexpr bool use_double_k_ = false;

      static constexpr int sst_footer_size_ = 32;

      static constexpr int sst_file_size_ = 16 * 1024;

      static constexpr int bloom_size_ = 8192;

      static constexpr int mem_max_size_ = sst_file_size_ - bloom_size_ - sst_footer_size_;

      static constexpr int kL0_CompactionTrigger = 4;

      static constexpr int kCompactionVLogLevel = 2;

      static constexpr int kBlockTrailerSize = 4;

      static constexpr int kMaxVLogSize = 128 * 1024 * 1024;

      static constexpr int block_size = 4096;

      static_assert(bloom_size_ > 0);

      Option &operator=(const Option &option) = delete;

      Option(const Option &option) = delete;

      static Option &getInstance() {
          static Option option;
          return option;
      }
  };
}// namespace LSMKV

#endif//OPTION_H
