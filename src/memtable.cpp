#include "memtable.h"

#include <mutex>

#include "utils/coding.h"
#include "lsmkv/dbformat.h"
#include "block_format.h"

namespace LSMKV {

  class MemTableIterator : public Iterator {
  public:
      explicit MemTableIterator(MemTable::Table *table) : _iter(table) {}

      MemTableIterator(const MemTableIterator &) = delete;

      MemTableIterator &operator=(const MemTableIterator &) = delete;

      ~MemTableIterator() override = default;

      [[nodiscard]] bool valid() const override { return _iter.hasNext(); }

      void seek(const Slice &k) override { _iter.seek(k); }

      void seekToFirst() override { _iter.seekToFirst(); }

      void next() override { _iter.next(); }

      void seek(const Slice &K1, const Slice &K2) override {
          _iter.seek(K1, K2);
      }

      Slice key() const override { return GetKeyFromMemKey(_iter.key()); }

      Slice value() const override { return GetValueFromMemKey(_iter.key()); }

  private:
      MemTable::Table::Iterator _iter;
  };

  Iterator *MemTable::newIterator() { return new MemTableIterator(&table); }

  bool MemTable::get(const QueryKey &qkey, VLogEntryInfo *res, Status *s) const {
      Table::Iterator iter(&table);
      iter.seek(qkey.mem_key());
      if (iter.hasNext()) {
          Slice key = iter.key();
          Slice internal_key = GetKeyFromMemKey(key);

          if (ExtractUserKey(internal_key) == qkey.user_key()) {
              if (ExtractSequenceNumber(internal_key) > ExtractSequenceNumber(qkey.internal_key())) {
                  return false;
              }

              switch (ExtractValueType(internal_key)) {
                  case ValueType::kTypeValue: {
                      *s = res->Decode(GetValueFromMemKey(key));
                      return true;
                  }
                  case ValueType::kTypeDeletion: {
                      *s = Status::NotFound("get a delete key in mem");
                      return true;
                  }
              }
          }
      }
      return false;
  }

  MemTable::MemTable() : arena(), table(&arena), size(0) {}

  Status MemTable::put(SequenceNumber seq, Slice key, Slice value) {
      auto vsz = value.size();
      // 8 is the size of sequence number and value type
      // 4 is the size of key size
      auto sz = 16 + 8 + key.size() + 4;

      auto type = static_cast<ValueType>(seq & 0xff);

      assert(type != ValueType::kTypeValue || value.size() == 16);
      assert(type != ValueType::kTypeDeletion || value.empty());


      char *buf = arena.allocate(sz);

      // format: key_size | key | seq_type | value

      EncodeFixed32(buf, key.size() + 8);

      utils::m_memcpy(buf + 4, key.data(), key.size());
      EncodeFixed64(buf + 4 + key.size(), seq);
      utils::m_memcpy(buf + 12 + key.size(), value.data(), vsz);

      size.fetch_add(1, std::memory_order_release);

      table.insert(Slice(buf, sz));

      return Status::OK();
  }

  MemTable::~MemTable() = default;

  uint32_t MemTable::memoryUsage() const { return arena.getUsed(); }

  bool MemTable::contains(const Slice &key) const { return table.contains(key); }

} // namespace LSMKV
