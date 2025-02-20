#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <functional>
#include <list>
#include <map>
#include <string>

#include "lsmkv/dbformat.h"
#include "format.h"
#include "option.h"
#include "skiplist.h"
#include "utils/arena.h"
#include "utils/coding.h"
#include "utils/iterator.h"
#include "utils/slice.h"
#include "utils/status.h"

namespace LSMKV {
  inline static Slice GetValueFromMemKey(Slice key);

  inline static Slice GetKeyFromMemKey(Slice key);

  struct MemKeyComporator {
      static int compare(const Slice &a, const Slice &b) {
          auto ka = GetKeyFromMemKey(a);
          auto kb = GetKeyFromMemKey(b);
          return InternalKeyComparator::compare_impl(ka, kb);
      }
  };

  class MemTable {
  public:
      using Table = Skiplist<Slice, MemKeyComporator>;

      explicit MemTable();

      Status put(SequenceNumber seq, Slice key, Slice value);

      bool get(const QueryKey& key, VLogEntryInfo *val, Status *s) const;

      bool contains(const Slice &key) const;

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
