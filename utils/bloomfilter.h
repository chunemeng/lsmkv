#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include "MurmurHash3.h"
#include "coding.h"
#include "src/include/option.h"
#include "slice.h"
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace LSMKV {
  static constexpr bool use_double_k = LSMKV::Option::use_double_k_;

  static constexpr int bloom_size = LSMKV::Option::bloom_size_;

  static inline void CreateFilter(Slice *keys, size_t n, std::string *dst) {
      uint32_t hash[4] = {0};

      auto filter_size = dst->size() * 8;
      char* array = dst->data();

      for (int i = 0; i < n; i++) {
          auto slice = keys[i];
          MurmurHash3_x64_128(slice.data(), slice.size(), 1, hash);
          for (uint32_t h: hash) {
              const uint32_t delta = (h >> 17) | (h << 15);
              if constexpr (use_double_k) {
                  const uint32_t delta = (h >> 17) | (h << 15);
                  for (int k = 0; k < 2; k++) {
                      const uint32_t bitpos = h % filter_size;
                      array[bitpos / 8] |= (1 << (bitpos % 8));
                      h += delta;
                  }
              } else {
                  const uint32_t bitpos = h % filter_size;
                  array[bitpos / 8] |= (1 << (bitpos % 8));
              }
          }
      }
  }


  static inline void CreateFilter(Slice *keys, size_t n, int len, size_t new_bloom_size, std::string *dst) {
      uint32_t hash[4] = {0};
      auto filter_size = new_bloom_size * 8;

      size_t bytes = new_bloom_size;
      const size_t init_size = dst->size();
      dst->resize(bytes);

      char *array = &(*dst)[init_size];

      for (int i = 0; i < n; i++) {
          auto slice = (keys + len * i);
          MurmurHash3_x64_128(slice->data(), slice->size(), 1, hash);
          for (uint32_t h: hash) {
              const uint32_t delta = (h >> 17) | (h << 15);
              for (int k = 0; k < 2; k++) {
                  const uint32_t bitpos = h % filter_size;
                  array[bitpos / 8] |= (1 << (bitpos % 8));
                  h += delta;
              }
          }
      }
  }

  static inline void CreateFilter(const char *keys, size_t n, int len, char *dst) {
      uint32_t hash[4] = {0};
      auto filter_size = bloom_size * 8;
      for (int i = 0; i < n; i++) {
          MurmurHash3_x64_128(keys + len * i, 8, 1, hash);
          for (uint32_t h: hash) {
              if constexpr (use_double_k) {
                  const uint32_t delta = (h >> 17) | (h << 15);
                  for (int k = 0; k < 2; k++) {
                      const uint32_t bitpos = h % filter_size;
                      dst[bitpos / 8] |= (1 << (bitpos % 8));
                      h += delta;
                  }
              } else {
                  const uint32_t bitpos = h % filter_size;
                  dst[bitpos / 8] |= (1 << (bitpos % 8));
              }
          }
      }
  }

  static inline bool KeyMayMatch(const uint64_t key, const Slice &bloom_filter) {
      const size_t len = bloom_filter.size();
      if (len < 2) return false;
      const char *array = bloom_filter.data();

      uint32_t hash[4] = {0};
      char buf[8];
      EncodeFixed64(buf, key);
      MurmurHash3_x64_128(buf, 8, 1, hash);

      return std::all_of(hash, hash + 4, [bits = len * 8, array](uint32_t h) {
          if constexpr (use_double_k) {
              const uint32_t delta = (h >> 17) | (h << 15);
              for (uint32_t k = 0; k < 2; k++) {
                  const uint32_t bitpos = h % bits;
                  if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) return false;
                  h += delta;
              }
              return true;
          } else {
              const uint32_t bitpos = h % bits;
              return (array[bitpos / 8] & (1 << (bitpos % 8))) != 0;
          }
      });
  }

  static inline bool KeyMayMatch(const Slice &key, const Slice &bloom_filter) {
      const size_t len = bloom_filter.size();
      if (len < 2) return false;

      const char *array = bloom_filter.data();

      uint32_t hash[4] = {0};
      MurmurHash3_x64_128(key.data(), key.size(), 1, hash);

      return std::all_of(hash, hash + 4, [bits = len * 8, array](uint32_t h) {
          if constexpr (use_double_k) {
              const uint32_t delta = (h >> 17) | (h << 15);
              for (uint32_t k = 0; k < 2; k++) {
                  const uint32_t bitpos = h % bits;
                  if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) return false;
                  h += delta;
              }
              return true;
          } else {
              const uint32_t bitpos = h % bits;
              return (array[bitpos / 8] & (1 << (bitpos % 8))) != 0;
          }
      });
  }
}// namespace LSMKV

#endif//BLOOMFILTER_H
