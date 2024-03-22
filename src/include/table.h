#ifndef LSMKV_SRC_TABLE_H
#define LSMKV_SRC_TABLE_H

#include "memtable.h"
#include "utils/bloomfilter.h"
#include "utils/coding.h"
#include "option.h"
#include "utils/comparator.h"
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>

namespace LSMKV {
//instance of sst
  class Table {
  private:
      using cmp = StrComparator;
  public:
      explicit Table(const uint64_t &file_no) : file_no(file_no) {
      }

      ~Table() = default;

      char *reserve(size_t size) {
          return arena.allocate(size);
      }

      explicit Table(const char *tmp, const uint64_t &file_no) {
          timestamp = DecodeFixed64(tmp);
          this->file_no = file_no;
          size_ = DecodeFixed64(tmp + 8);

          char *buf;
          if constexpr (LSMKV::Option::isFilter) {
              buf = arena.allocate(size_ * 20 + bloom_size);
              bloom = Slice(buf, bloom_size);
          } else {
              buf = arena.allocate(size_ * 20);
          }
          // todo: variable length key
          min_key = Slice(tmp + 16, 8);
          max_key = Slice(tmp + 24, 8);

          // NO NEED TO COPY HEADER
          // IF OPEN FILTER COPY IT
          memcpy(buf, tmp + 32 + kMemOffset, kOffset + size_ * 28);
          uint64_t offset = kOffset;

          for (uint64_t i = 0; i < size_ * 28; i += 28) {
              // TODO: variable length key

              sst.emplace_back(Slice(buf + offset + i, 16), Slice(buf + offset + 8 + i, 12));
//              sst[i / 20] = std::make_pair(Slice(buf + offset + i, 8), Slice(buf + offset + 8 + i, 12));
          }
      }

      void pushCache(const char *tmp) {
          if constexpr (LSMKV::Option::isFilter) {
              bloom = Slice(tmp + 32, bloom_size);
          }
          timestamp = DecodeFixed64(tmp);
          size_ = DecodeFixed64(tmp + 8);
          uint64_t offset = bloom_size + 32;
          min_key = Slice(tmp + 16, 8);
          max_key = Slice(tmp + 24, 8);


          for (uint64_t i = 0; i < size_ * 28; i += 28) {
              sst.emplace_back(Slice(tmp + offset + i, 28), Slice(tmp + offset + 8 + i, 12));
              // TODO: variable length key
          }
      }

      [[nodiscard]] uint64_t GetTimestamp() const {
          return timestamp;
      }

      [[nodiscard]] bool KeyMatch(const Slice &key) const {
          if (key < sst[0].first || key > sst[size() - 1].first) {
              return false;
          }
          if constexpr (!LSMKV::Option::isFilter) {
              return true;
          }
          return KeyMayMatch(key, bloom);
      }

      bool operator<(const Table &rhs) const {
          return timestamp < rhs.timestamp;
      }

      bool operator<(const uint64_t &rhs) const {
          return timestamp < rhs;
      }

      std::string get(const Slice &key) {
          if (!KeyMatch(key)) {
              return {};
          }
          uint64_t offset = BinarySearchGet(key);
          if (offset >= size()) {
              return {};
          } else {
              return {sst[offset].second.data(), sst[offset].second.size()};
          }
      }

      size_t size() const {
          return size_;
      }

  private:
      [[nodiscard]] int32_t BinarySearchGet(const Slice &key) const {
          // NOT INCLUDE END
          int32_t left = 0, right = static_cast<int32_t>(size()), mid;
          while (left < right) {
              mid = left + ((right - left) >> 1);
              sst[mid].first < key ? left = mid + 1 : right = mid;
          }
          // To avoid False positive
          if (left <= size() && sst[left].first == key) {
              return left;
          }

          return static_cast<int32_t>(size());
      }

      [[nodiscard]] int32_t BinarySearchLocation(int32_t start,
                                                 int32_t end,
                                                 const Slice &key) const {
          // NOT INCLUDE END
          int32_t left = start, right = end, mid;
          while (left < right) {
              mid = left + ((right - left) >> 1);
              sst[mid].first < key ? left = mid + 1 : right = mid;
          }
          return left;
      }

      Arena arena;
      Slice bloom;
      uint64_t file_no = 0;
      uint64_t timestamp = 0;
      size_t size_ = 0;
      Slice min_key{};
      Slice max_key{};

      std::vector<std::pair<Slice, Slice>> sst{};


      static constexpr size_t kOffset = Option::isFilter * bloom_size;
      static constexpr size_t kMemOffset = !kOffset * bloom_size;

  public:
      static constexpr size_t pair_size = sizeof(decltype(sst[0]));

      class TableIterator {
      public:
          explicit TableIterator(Table *table) : _table(table) {};

          void seekToEmpty() {
              setTimestamp(0);
              _end = 0;
          }

          ~TableIterator() {
              delete _table;
              _table = nullptr;
          }

          [[nodiscard]] uint64_t size() const {
              return _table->size();
          }

          [[nodiscard]] bool hasNext() const {
              return _end > _cur;
          }

          [[nodiscard]] Slice value() const {
              return _table->sst[_cur].second;
          }

          bool isEnd() const {
              return _cur >= _end;
          }

          [[nodiscard]] Slice merge_key() const {
              assert(_cur < _end);
              return _table->sst[_cur].first;
          }

          void merge_next() {
              if (_cur + 1 == _end) [[unlikely]] {
                  seekToEmpty();
              }
              _cur++;
          }

          [[nodiscard]] const Slice &key() const {
              return _table->sst[_cur].first;
          }

          [[nodiscard]] bool AtStart() const {
              return _cur == 0;
          }


          void next() {
              _cur++;
          }

          void seek(const Slice &key) {
              if (!_table->KeyMatch(key)) {
                  _end = 0;
                  _cur = 1;
                  return;
              }
              _cur = _table->BinarySearchGet(key);
              _end = static_cast<int32_t>(_table->size());
          }

          void seek(const Slice &K1, const Slice &K2) {
              if (!InRange(K1, K2)) {
                  _cur = _end + 1;
              }
              _end = static_cast<int32_t>(_table->size());
              _cur = _table->BinarySearchLocation(0, _end, K1);
              _end = _table->BinarySearchLocation(_cur, _end, K2);
              if (_end < _table->size() && _table->sst[_end].first == K2) {
                  _end++;
              }
          }

          void seekToLast() {
              _end = static_cast<int32_t>(_table->size());
              _cur = _end - 1;
          }

          [[nodiscard]] Slice LargestKey() const {
              return _table->sst[_table->size() - 1].first;
          }

          [[nodiscard]] Slice SmallestKey() const {
              return _table->sst[0].first;
          }

          void setFileNo(const uint64_t &file_no) {
              _table->file_no = file_no;
          }

          void setTimestamp(const uint64_t &timestamp) {
              _table->timestamp = timestamp;
          }


          [[nodiscard]] bool InRange(const Slice &smallest, const Slice &largest) const {
              Slice s_key = ExtractUserKey(smallest);
              Slice l_key = ExtractUserKey(largest);
              // TODO variable length key

              return std::max(s_key, SmallestKey()) <= std::min(l_key, LargestKey());
          }

          void seekToFirst() {
              _cur = 0;
              _end = static_cast<int32_t>(_table->size());
          }

          [[nodiscard]] uint64_t timestamp() const {
              return _table->timestamp;
          }

          [[nodiscard]] uint64_t file_no() const {
              return _table->file_no;
          }

      private:
          Table *_table;
          // not include _end!!!!
          int32_t _cur{};
          int32_t _end{};
      };

      TableIterator *Iterator() {
          return new Table::TableIterator(this);
      }
  };

}// namespace LSMKV

#endif//LSMKV_SRC_TABLE_H
