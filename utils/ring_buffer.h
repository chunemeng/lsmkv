#pragma once

#include <array>
#include <cstdint>

#include "slice.h"

namespace LSMKV {

  template<uint32_t N>
  struct RingBuffer {

      uint32_t Mask(uint32_t n) const {
          if constexpr ((N & (N - 1)) == 0) {
              return n & (N - 1);
          } else {
              return n % N;
          }
      }

      uint32_t Size() const {
          return tail_ - head_;
      }

      uint32_t Append(const Slice &s) {
          auto write_size = std::min(static_cast<uint32_t>(s.size()), N - Size());

          if (write_size <= 10) {
              return 0;
          }

          auto copy_size = std::min(write_size, N - Mask(tail_));
          utils::m_memcpy(buffer_.data() + Mask(tail_), s.data(), copy_size);
          tail_ += copy_size;

          if (copy_size < write_size) {
              const auto remaining_size = write_size - copy_size;
              utils::m_memcpy(buffer_.data(), s.data() + copy_size, remaining_size);
              tail_ += remaining_size;
          }

          return write_size;
      }

      void Free(uint32_t n) {
          head_ += n;
      }

      uint32_t head_ = 0;
      uint32_t tail_ = 0;
      std::array<char, N> buffer_;
  };

} // namespace LSMKV
