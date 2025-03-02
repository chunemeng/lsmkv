#pragma once

#include "src/include/kvstore.h"
#include <random>


namespace LSMKV::testutils {

  static inline std::string Key(uint64_t i) {
      std::string res;
      res.resize(8);
      EncodeFixed64(res.data(), std::byteswap(i));
      return res;
  }

  inline void put(int length, const std::string &s, KVStore *store) {
      for (int i = 0; i < length; i++) {
          store->put(Key(i), s);
      }
  }


  Slice RandomString(std::mt19937 *rnd, int len, std::string *dst);


  std::string RandomKey(std::mt19937 *rnd, int len);

} // namespace LSMKV::testutils