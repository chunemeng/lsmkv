#include <utility>
#include <mutex>
#include <shared_mutex>

#include "memtable.h"
#include "utils/coding.h"
#include "lsmkv/dbformat.h"
#include "format.h"

namespace LSMKV {

  static Slice GetValueFromMemKey(Slice key) {
      auto sz = DecodeFixed32(key.data());

      auto val_sz = key[sz + 4];
      // 4 is the size of key
      // 1 is the size of value size
      return {key.data() + sz + 5, static_cast<size_t>(val_sz)};
  }

  static Slice GetKeyFromMemKey(Slice key) {
      return {key.data() + 4, DecodeFixed32(key.data())};
  }

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
                      *s = Status::NotFound();
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
      // 1 is the size of value size
      auto sz = vsz + 8 + key.size() + 4 + 1;

      auto type = static_cast<ValueType>(seq & 0xff);

      switch (type) {
          case ValueType::kTypeValue:
              // this value is VLogInfo in vlog_builder.h
              if (vsz != 16) {
                  return Status::Corruption("bad value size");
              }
              break;
          case ValueType::kTypeDeletion:
              if (vsz != 0) {
                  return Status::Corruption("bad deletion size");
              }
              break;
          default:
              return Status::Corruption("unknown value type");
      }


      assert(!type == ValueType::kTypeValue || value.size() == 16);
      assert(!type == ValueType::kTypeDeletion || value.empty());


      char *buf = arena.allocate(sz);

      // format: key_size | key | seq_type | value

      EncodeFixed32(buf, key.size() + 8);
      memcpy(buf + 4, key.data(), key.size());
      EncodeFixed64(buf + 4 + key.size(), seq);
      EncodeFixed8(buf + 4 + key.size() + 8, static_cast<uint8_t>(vsz));

      if (vsz > 0) {
          memcpy(buf + 12 + key.size() + 1, value.data(), vsz);
      }

      size.fetch_add(sz, std::memory_order_release);

      table.insert(Slice(buf, sz));

      return Status::OK();
  }

  MemTable::~MemTable() = default;

  uint32_t MemTable::memoryUsage() const { return size.load(std::memory_order_acquire); }

  bool MemTable::contains(const Slice &key) const { return table.contains(key); }

} // namespace LSMKV
