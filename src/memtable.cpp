#include <utility>

#include "memtable.h"
#include "utils/coding.h"
#include "dbformat.h"

namespace LSMKV {

  static Slice GetValueFromInternalKey(Slice key) {
      return {key.data() + key.size() - 12, 12};
  }

  static Slice GetKeyFromInternalKey(Slice key) {
      const char *p = key.data();
      // user key length
      uint32_t len = DecodeFixed32(p);

      // skip user key and sequence number
      return {p + 4, len};
  }

  class MemTableIterator : public Iterator {
  public:
      explicit MemTableIterator(MemTable::Table *table) : _iter(table) {}

      MemTableIterator(const MemTableIterator &) = delete;

      MemTableIterator &operator=(const MemTableIterator &) = delete;

      ~MemTableIterator() override = default;

      [[nodiscard]] bool hasNext() const override { return _iter.hasNext(); }

      void seek(const Slice &k) override { _iter.seek(k); }

      void seekToFirst() override { _iter.seekToFirst(); }

      void next() override { _iter.next(); }

      void scan(const Slice &K1, const Slice &K2,
                std::list<std::pair<std::string, std::string>> &list) override {
          _iter.seek(K1, K2);
          for (; _iter.hasNext(); _iter.next()) {
              list.emplace_back(key(), value());
          }
      }

      Slice key() const override { return GetKeyFromInternalKey(_iter.key()); }

      Slice value() const override { return GetValueFromInternalKey(_iter.key()); }

  private:
      MemTable::Table::Iterator _iter;
  };

  Iterator *MemTable::newIterator() { return new MemTableIterator(&table); }

  bool MemTable::get(SequenceNumber seq, Slice key, Slice &res, status &s) const {
      QueryKey qkey{seq, ValueType::kTypeValue, key};
      Table::Iterator iter(&table);
      iter.seek(qkey.mem_key());
      if (iter.hasNext()) {
          if (ExtractUserKey(iter.key()) == key) {
              switch (ExtractValueType(iter.key())) {
                  case ValueType::kTypeValue: {
                      s = status::OK();
                      res = GetValueFromInternalKey(iter.key());
                      return true;
                  }
                  case ValueType::kTypeDeletion: {
                      s = status::notFound();
                      return true;
                  }
              }
          }
      }
      return false;
  }

  MemTable::MemTable() : arena(), table(&arena), size(0) {}

  void MemTable::del(SequenceNumber seq, ValueType type, Slice key) {
      auto sz = 8 + key.size() + 4;

      char *buf = arena.allocate(sz);
      EncodeFixed32(buf, key.size());
      memcpy(buf + 4, key.data(), key.size());
      EncodeFixed64(buf + 4 + key.size(), (seq << 8) | type);

      size += key.size() + 12;
      table.insert(std::move(Slice(buf, sz)));
  }

  void MemTable::put(SequenceNumber seq, ValueType type, Slice key, Slice value) {
      //      assert(type == ValueType::kTypeValue);
      auto sz = 12 + 8 + key.size() + 4;

      char *buf = arena.allocate(sz);
      EncodeFixed32(buf, key.size());
      memcpy(buf + 4, key.data(), key.size());
      EncodeFixed64(buf + 4 + key.size(), (seq << 8) | type);
      assert(value.size() == 16);
      memcpy(buf + 12 + key.size(), value.data(), value.size());

      size += key.size() + 28;
      table.insert(std::move(Slice(buf, sz)));
  }

  MemTable::~MemTable() = default;

  size_t MemTable::memoryUsage() const { return size; }

  bool MemTable::contains(const Slice &key) const { return table.contains(key); }

} // namespace LSMKV
