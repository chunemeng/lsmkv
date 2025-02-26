#include "gtest/gtest.h"
#include "../src/include/kvstore.h"

namespace LSMKV {
  static inline std::string Key(uint64_t i) {
      std::string res;
      res.resize(8);
      EncodeFixed64(res.data(), std::byteswap(i));
      return res;
  }

  class SameTimeStampTest : public testing::Test {
  public:
      SameTimeStampTest() : store("/home/data", "/home/data/vlog") { store.reset(); }

      ~SameTimeStampTest() override = default;

      void Build() {
          std::string key{"abcdefg"};
          std::string not_equal{"gfedcba"};
          const int step = 408;
          int start{};
          store.reset();

          for (int j = 1; j < 7; j++) {
              start = 1000 * j;
              for (int i = start; i < start + step; i++) {
                  store.put(Key(i), not_equal);
              }
          }

          start = 1600;
          for (int i = start; i < start + step; ++i) {
              store.put(Key(i), not_equal);
          }
          start = 2200;
          for (int i = start; i < start + step; ++i) {
              store.put(Key(i), not_equal);
          }

          start = 3000;
          for (int i = start; i < start + step; ++i) {
              store.put(Key(i), not_equal);
          }


          for (int i = 1980; i < 1980 + 408 * 20; i += 20) {
              store.put(Key(i), key);
          }

          for (int i = 1980; i < 1980 + 408 * 20; i += 20) {
              store.put(Key(i), key);
          }

          for (int i = 1980; i < 1980 + 408 * 20; i += 20) {
              store.put(Key(i), key);
          }

          start = 10000;
          for (int j = 0; j < 3; ++j)
              for (int i = start; i < start + step; i++) {
                  store.put(Key(i), key);
              }
          store.put(Key(100000), key);
      }

  public:
      KVStore store;
  };

  TEST_F(SameTimeStampTest, SameTimeStamp) {
      Build();
      std::string not_equal{"gfedcba"};
      int start = 2200;
      for (int i = 0; i < 208; ++i) {
          if (i % 20 == 0) {
              continue;
          } else {
              EXPECT_EQ(store.get(Key(i + start)), not_equal);
          }
      }

  }
} // namespace LSMKV
