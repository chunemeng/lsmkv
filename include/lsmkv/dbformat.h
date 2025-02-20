#pragma once

#include <string>

#include "utils/slice.h"
#include "utils/coding.h"

namespace LSMKV {
  enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };

  using SequenceNumber = uint64_t;

  class QueryKey {
      uint32_t len_;
      std::string rep_;

  public:
      QueryKey(SequenceNumber seq, Slice key) {
          // 4 bytes for key length, key, 8 bytes for seq and type
          len_ = key.size() + 12;
          rep_.resize(len_);
          char *p = rep_.data();
          EncodeFixed32(p, key.size() + 8);
          memcpy(p + 4, key.data(), key.size());

          // it okays to just put the value type as the 0x0
          EncodeFixed64(p + 4 + key.size(), (seq << 8));
      }

      SequenceNumber sequence() const {
          return DecodeFixed64(rep_.data() + len_ - 8) >> 8;
      }

      Slice user_key() const { return {rep_.data() + 4, len_ - 12}; }

      Slice mem_key() const { return {rep_.data(), len_}; }

      Slice internal_key() const { return {rep_.data() + 4, len_ - 4}; }
  };

  static inline Slice ExtractUserKey(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return {internal_key.data(), internal_key.size() - 8};
  }

  static inline ValueType ExtractValueType(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return static_cast<ValueType>(internal_key.data()[internal_key.size() - 8]);
  }

  static inline SequenceNumber ExtractSequenceNumber(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return DecodeFixed64(internal_key.data() + internal_key.size() - 8) >> 8;
  }

  class InternalKey {
  public:
      InternalKey() {}

      bool DecodeFrom(const Slice &s) {
          rep_.assign(s.data(), s.size());
          return !rep_.empty();
      }

      Slice user_key() const { return ExtractUserKey(rep_); }

      ValueType type() const { return ExtractValueType(rep_); }

      SequenceNumber sequence() const {
          return DecodeFixed64(rep_.data() + rep_.size() - 8) >> 8;
      }

      Slice Encode() const {
          assert(!rep_.empty());
          return rep_;
      }

  private:
      std::string rep_;
  };

  static constexpr SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);

  struct ParsedInternalKey {
      Slice user_key;
      SequenceNumber sequence;
      ValueType type;

      ParsedInternalKey() {}

      ParsedInternalKey(const Slice &u, const SequenceNumber &seq, ValueType t)
              : user_key(u), sequence(seq), type(t) {}

      ParsedInternalKey(const InternalKey &ikey) {
          user_key = ikey.user_key();
          sequence = ikey.sequence();
          type = ikey.type();
      }
  };


}// namespace LSMKV