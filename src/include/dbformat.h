#pragma once

#include "utils/slice.h"

namespace LSMKV {
  enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1, kTypeLittleValue = 0x2 };

  using SequenceNumber = uint64_t;

  static inline Slice ExtractUserKey(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return {internal_key.data(), internal_key.size() - 8};
  }

  static inline ValueType ExtractValueType(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return static_cast<ValueType>(internal_key.data()[internal_key.size() - 8]);
  }

  class InternalKey {
  public:
      InternalKey() {}

      InternalKey(Slice user_key, SequenceNumber s, ValueType t) {
          rep_.reserve(user_key.size() + 8);
          rep_.assign(user_key.data(), user_key.size());
          rep_.resize(user_key.size() + 8);
          EncodeFixed64(&rep_[user_key.size()], (s << 8) | t);
      }

      bool DecodeFrom(const Slice &s) {
          rep_.assign(s.data(), s.size());
          return !rep_.empty();
      }

      Slice user_key() const { return ExtractUserKey(rep_); }

      ValueType type() const { return ExtractValueType(rep_); }

      SequenceNumber sequence() const {
          return DecodeFixed64(rep_.data() + rep_.size() - 8) >> 8;
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