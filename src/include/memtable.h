#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <functional>
#include <list>
#include <map>
#include <string>

#include "lsmkv/dbformat.h"
#include "block_format.h"
#include "option.h"
#include "skiplist.h"
#include "utils/arena.h"
#include "utils/coding.h"
#include "utils/iterator.h"
#include "utils/slice.h"
#include "utils/status.h"

namespace LSMKV {
  inline static Slice GetValueFromMemKey(Slice key) {
      auto sz = DecodeFixed32(key.data());

      auto val_sz = key[sz + 4];
      // 4 is the size of key
      // 1 is the size of value size
      return {key.data() + sz + 5, static_cast<size_t>(val_sz)};
  }

  inline static Slice GetKeyFromMemKey(Slice key) {
      return {key.data() + 4, DecodeFixed32(key.data())};
  }

  struct MemKeyComporator {
      static int compare(const Slice &a, const Slice &b) {
          auto ka = GetKeyFromMemKey(a);
          auto kb = GetKeyFromMemKey(b);
          return InternalKeyComparator::compare_impl(ka, kb);
      }

      static int compare_user_key(const Slice &a, const Slice &b) {
          auto ka = GetKeyFromMemKey(a);
          auto kb = GetKeyFromMemKey(b);
          return StrComparator::compare_impl(ExtractUserKey(ka), ExtractUserKey(kb));
      }
  };

  class MemTable {
  public:
      using Table = Skiplist<Slice, MemKeyComporator>;

      explicit MemTable();

      Status put(SequenceNumber seq, Slice key, Slice value);

      bool get(const QueryKey &key, VLogEntryInfo *val, Status *s) const;

      bool contains(const Slice &key) const;

      auto Size() const { return size.load(); }

      Iterator *newIterator();

      uint32_t memoryUsage() const;

      ~MemTable();

  private:
      // satisfy the concept of Table
      friend class MemTableIterator;

      std::atomic<uint32_t> size = 0;
      Arena arena;
      Table table;
  };
} // namespace LSMKV

#endif // MEMTABLE_H
