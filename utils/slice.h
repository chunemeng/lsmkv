#ifndef SLICE_H
#define SLICE_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <limits>
#include <string_view>
#include <iostream>
#include "comparator.h"

namespace utils {
  static inline void m_memcpy(void *__restrict _dest, const void *__restrict _src,
                              size_t _n) {
      if (_n > 0)[[likely]] {
          memcpy(_dest, _src, _n);
      }
  }

} // namespace utils

namespace LSMKV {
  class Slice : public std::string_view {
  public:
      Slice() : std::string_view() {
      }

      constexpr Slice(const char *data, size_t size) : std::string_view(data, size) {
      }

      constexpr Slice(const char *data) : std::string_view(data) {
      }

      Slice(const std::string &str) : std::string_view(str) {
      }

      Slice(const Slice &sc) = default;

      Slice(Slice &&sc) noexcept = default;

      Slice &operator=(Slice &&) = default;

      Slice &operator=(const Slice &) = default;

      void clear() {
          this->operator=("");
      }

      [[nodiscard]] std::string toString() const {
          return {this->data(), this->size()};
      }
  };
}// namespace LSMKV

#endif//SLICE_H
