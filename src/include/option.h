#ifndef OPTION_H
#define OPTION_H

namespace LSMKV {
  struct Option {
      Option() = default;

      ~Option() = default;

      static constexpr bool isFilter = true;
      static constexpr int pair_size_ = 290;
      static constexpr bool use_double_k_ = false;
      static constexpr bool use_rb_tree_ = false;
      static constexpr int key_size_ = pair_size_ * 20;
      static constexpr int k_header_size_ = 32;
      static constexpr int sst_file_size_ = 16 * 1024;
      static constexpr int bloom_size_ = 8192;
      static constexpr int k_vlog_header_size_ = 15;

      static constexpr int mem_max_size_ = sst_file_size_ - bloom_size_ - k_header_size_;


      static constexpr bool sync_wal_ = true;

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
