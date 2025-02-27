#include "utils/comparator.h"
#include "utils/slice.h"
#include "utils/coding.h"
#include "lsmkv/dbformat.h"

namespace LSMKV {
  namespace detail {
    AnyComparator::AnyComparator(UserDefinedComparator *comparator)
            : comparator_(comparator) {}
  } // namespace detail



  int LSMKV::StrComparator::compare_impl(const LSMKV::Slice &a, const LSMKV::Slice &b) {
      return a.compare(b);
  }

  void LSMKV::StrComparator::find_shortest_separator_impl(std::string *start, const LSMKV::Slice &limit) {
      Slice s = *start;
      Slice l = limit;
      size_t min_length = std::min(s.size(), l.size());
      size_t diff_index = 0;
      while (diff_index < min_length && s[diff_index] == l[diff_index]) {
          diff_index++;
      }
      if (diff_index >= min_length) {
      } else {
          auto diff_byte = static_cast<uint8_t>(s[diff_index]);
          if (diff_byte < 0xff && diff_byte + 1 < static_cast<uint8_t>(l[diff_index])) {
              (*start)[diff_index]++;
              start->resize(diff_index + 1);
              assert(compare_impl(*start, limit) < 0);
          }
      }
  }

  int NumComparator::compare_impl(const Slice &a, const Slice &b) {
      assert(a.size() == b.size());

      uint64_t l = DecodeFixed64(a.data());
      uint64_t r = DecodeFixed64(b.data());

      return (l > r) - (l < r);
  }

  int InternalKeyComparator::compare_impl(const Slice &a, const Slice &b) {
      int res = StrComparator::compare_impl(extract_user_key(a), extract_user_key(b));
      // if user key is the same, compare sequence number by descending order
      return res ? res : -(NumComparator::compare_impl(extract_seq_num(a), extract_seq_num(b)));
  }

  Slice InternalKeyComparator::extract_user_key(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return {internal_key.data(), internal_key.size() - 8};
  }

  Slice InternalKeyComparator::extract_seq_num(const Slice &internal_key) {
      assert(internal_key.size() >= 8);
      return {internal_key.data() + internal_key.size() - 8, 8};
  }

  int Comparator::compare(const Slice &a, const Slice &b) const {
      return std::visit([a, b](auto &&c) { return c.compare(a, b); }, comparator_);
  }

  void Comparator::find_shortest_separator(std::string *start, const Slice &limit) const {
      std::visit([start, limit](auto &&c) { return c.find_shortest_separator(start, limit); }, comparator_);
  }

  const char *Comparator::name() const {
      return std::visit([](auto &&c) { return c.name(); }, comparator_);
  }

  void Comparator::find_short_successor(std::string *key) const {
      std::visit([key](auto &&c) { return c.find_short_successor(key); }, comparator_);
  }

  int UserKeyComparator::compare_impl(const Slice &a, const Slice &b) {
      return StrComparator::compare_impl(ExtractUserKey(a), ExtractUserKey(b));
  }


} // namespace LSMKV