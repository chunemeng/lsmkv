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
namespace LSMKV {
  class WriteSlice {
  public:
      WriteSlice(char *data, size_t size) : _data(data), _size(size) {
      }

      char *data() {
          return _data;
      }

      size_t size() const {
          return _size;
      }

  private:
      char *_data;
      size_t _size;
  };


  class Slice : public std::string_view {
      using Comparator = StrComparator;
  public:
      Slice() : std::string_view() {
      }

      constexpr Slice(const char *data, size_t size) : std::string_view(data, size) {
      }

      Slice(const std::string &str) : std::string_view(str) {
      }

      Slice(const char *data) : std::string_view(data) {
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
