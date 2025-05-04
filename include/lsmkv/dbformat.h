#pragma once

#include <string>
#include <utility>
#include <memory>
#include <utils/status.h>

#include "utils/slice.h"
#include "utils/coding.h"
#include "utils/utils.h"

namespace LSMKV {
  enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };

  using SequenceNumber = uint64_t;

  class QueryKey {
      uint32_t len_;
      std::string rep_;

  public:
      QueryKey(SequenceNumber seq, Slice key) {
          // 4 bytes for key length | key | 8 bytes for seq and type
          len_ = key.size() + 12;
          rep_.resize(len_);
          char *p = rep_.data();
          EncodeFixed32(p, key.size() + 8);
          utils::m_memcpy(p + 4, key.data(), key.size());

          // it okays to just put the value type as the 0x0
          EncodeFixed64(p + 4 + key.size(), (seq << 8));
      }

      SequenceNumber sequence() const {
          return DecodeFixed64(rep_.data() + len_ - 8) >> 8;
      }

      // only key
      Slice user_key() const { return {rep_.data() + 4, len_ - 12}; }

      // size | key | seq | type
      Slice mem_key() const { return {rep_.data(), len_}; }

      // key | seq | type
      Slice internal_key() const { return {rep_.data() + 4, len_ - 4}; }
  };

  static inline Slice ExtractUserKey(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return {internal_key.data(), internal_key.size() - 8};
  }

  static inline ValueType ExtractValueType(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return static_cast<ValueType>(DecodeFixed64(internal_key.data() + internal_key.size() - 8) & 0xff);
  }

  static inline SequenceNumber ExtractSequenceNumber(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return DecodeFixed64(internal_key.data() + internal_key.size() - 8) >> 8;
  }

  class InternalKey {
  public:
      InternalKey() = default;

      InternalKey(InternalKey &&) noexcept = default;

      InternalKey &operator=(InternalKey &&) noexcept = default;

      InternalKey(const InternalKey &) = delete;

      InternalKey &operator=(const InternalKey &) = delete;

      void DecodeFrom(const Slice &s) {
          rep_.reset();
          rep_ = std::make_unique<char[]>(s.size());
          utils::m_memcpy(rep_.get(), s.data(), s.size());
          size_ = s.size();
      }

      void Decode(const Slice &s) {
          DecodeFrom(s);
      }

      Slice user_key() const { return ExtractUserKey(Encode()); }

      ValueType type() const { return ExtractValueType(Encode()); }

      SequenceNumber sequence() const {
          return DecodeFixed64(rep_.get() + size_ - 8) >> 8;
      }

      Slice Encode() const {
          assert(size_ != 0);
          return {rep_.get(), size_};
      }

      uint32_t size() const { return size_; }

  private:
      uint32_t size_;

      // same as a key & (seq | type)
      std::unique_ptr<char[]> rep_;
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