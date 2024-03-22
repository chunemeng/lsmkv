#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <functional>
#include <list>
#include <map>
#include <string>

#include "dbformat.h"
#include "option.h"
#include "skiplist.h"
#include "utils/arena.h"
#include "utils/coding.h"
#include "utils/iterator.h"
#include "utils/slice.h"
#include "utils/status.h"

namespace LSMKV {
using key_type = Slice;
using value_type = Slice;
class QueryKey {
  uint32_t len_;
  std::unique_ptr<char[]> rep_;

public:
  QueryKey(SequenceNumber seq, ValueType type, key_type key) {
    len_ = key.size() + 12;
    rep_ = std::make_unique<char[]>(len_);
    char *p = rep_.get();
    EncodeFixed32(p, key.size());
    memcpy(p + 4, key.data(), key.size());
    EncodeFixed64(p + 4 + key.size(), (seq << 8) | type);
  }

  Slice mem_key() const { return {rep_.get(), len_}; }
};

inline static Slice GetValueFromInternalKey(Slice key);

inline static Slice GetKeyFromInternalKey(Slice key);

struct MemKeyComporator {
  static int compare(const key_type &a, const key_type &b) {
    auto ka = GetKeyFromInternalKey(a);
    auto kb = GetKeyFromInternalKey(b);
    return InternalKeyComparator::compare(ka, kb);
  }
};

class MemTable {
public:
  using Table = Skiplist<key_type, MemKeyComporator>;

  explicit MemTable();

  void put(SequenceNumber seq, ValueType type, Slice key, Slice value);

  void del(SequenceNumber seq, ValueType type, Slice key);

  bool get(SequenceNumber seq, key_type key, Slice &val, status &s) const;

  bool contains(const Slice &key) const;

  Iterator *newIterator();

  [[nodiscard]] size_t memoryUsage() const;

  ~MemTable();

private:
  // satisfy the concept of Table
  friend class MemTableIterator;

  uint64_t size = 0;
  Arena arena;
  Table table;
};
} // namespace LSMKV

#endif // MEMTABLE_H
