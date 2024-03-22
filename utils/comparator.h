#pragma once

#include <utility>
#include <string>

namespace LSMKV {
  class Slice;

  template<typename T>
  concept comparator = requires {
      { T::compare_impl(std::declval<const Slice &>(), std::declval<const Slice &>()) } -> std::convertible_to<int>;
      {
      T::find_shortest_separator_impl(std::declval<std::string *>(), std::declval<const Slice &>())
      } -> std::convertible_to<void>;
      { T::find_short_successor_impl(std::declval<std::string *>()) } -> std::convertible_to<void>;
      { T::name_impl() } -> std::convertible_to<const char *>;
  };

  template<typename Impl>
  class Comparator {
  public:
      Comparator() {
          static_assert(comparator<Impl>, "Impl must satisfy the comparator concept");
      }

      static int compare(const Slice &a, const Slice &b) {
          return Impl::compare_impl(a, b);
      }

      static void find_shortest_separator(std::string *start, const Slice &limit) {
          return Impl::find_shortest_separator_impl(start, limit);
      }

      static void find_short_successor(std::string *key) {
          return Impl::find_short_successor_impl(key);
      }

      static const char *name() {
          return Impl::name_impl();
      }
  };

  class InternalKeyComparator : public Comparator<InternalKeyComparator> {
  private:
      static Slice extract_user_key(const Slice &internal_key);

      static Slice extract_seq_num(const Slice &internal_key);

  public:
      static int compare_impl(const Slice &a, const Slice &b);

      static void find_shortest_separator_impl(std::string *start, const Slice &limit);

      static void find_short_successor_impl(std::string *key);

      static const char *name_impl() {
          return "InternalKeyComparator";
      }
  };


  class StrComparator : public Comparator<StrComparator> {
  public:
      static int compare_impl(const Slice &a, const Slice &b);

      static void find_shortest_separator_impl(std::string *start, const Slice &limit);

      static void find_short_successor_impl(std::string *key) {
          for (size_t i = 0; i < key->size(); i++) {
              if ((*key)[i] != 0xff) {
                  (*key)[i]++;
                  key->resize(i + 1);
                  return;
              }
          }
      }

      static const char *name_impl() {
          return "KeyComparator";
      }

  };

  class NumComparator : public Comparator<NumComparator> {
  public:
      static int compare_impl(const Slice &a, const Slice &b);

      static void find_shortest_separator_impl(std::string *start, const Slice &limit) {
      }

      static void find_short_successor_impl(std::string *key) {
          for (size_t i = 0; i < key->size(); i++) {
              if ((*key)[i] != 0xff) {
                  (*key)[i]++;
                  key->resize(i + 1);
                  return;
              }
          }
      }

      static const char *name_impl() {
          return "NumComparator";
      }
  };
}  // namespace LSMKV