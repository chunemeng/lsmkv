#pragma once

#include <utility>
#include <string>
#include <cassert>
#include <type_traits>
#include <variant>

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
  class ComparatorImpl {
  public:
      ComparatorImpl() {
          static_assert(comparator<Impl>, "Impl must satisfy the comparator concept");
          static_assert(!std::is_reference_v<Impl>, "Impl must not be a reference type");
      }

      int compare(const Slice &a, const Slice &b) const {
          return static_cast<const Impl *>(this)->compare_impl(a, b);
      }

      void find_shortest_separator(std::string *start, const Slice &limit) const {
          return static_cast<const Impl *>(this)->find_shortest_separator_impl(start, limit);
      }

      void find_short_successor(std::string *key) const {
          return static_cast<const Impl *>(this)->find_short_successor_impl(key);
      }

      const char *name() const {
          return static_cast<const Impl *>(this)->name_impl();
      }
  };

  class InternalKeyComparator : public ComparatorImpl<InternalKeyComparator> {
  private:
      static Slice extract_user_key(const Slice &internal_key);

      static Slice extract_seq_num(const Slice &internal_key);

  public:
      static int compare_impl(const Slice &a, const Slice &b);

      static void find_shortest_separator_impl(std::string *start, const Slice &limit) {
          assert(false);
          return;
      }

      static void find_short_successor_impl(std::string *key) {
          assert(false);
          return;
      }

      static const char *name_impl() {
          return "InternalKeyComparator";
      }
  };


  class StrComparator : public ComparatorImpl<StrComparator> {
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

  class NumComparator : public ComparatorImpl<NumComparator> {
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

  struct UserKeyComparator : public ComparatorImpl<UserKeyComparator> {
      static int compare_impl(const Slice &a, const Slice &b);

      static void find_shortest_separator_impl(std::string *start, const Slice &limit) {
          assert(false);
      }

      static void find_short_successor_impl(std::string *key) {
          assert(false);
      }

      static const char *name_impl() {
          return "UserKeyComparator";
      }
  };

  using ComparatorType = std::variant<InternalKeyComparator, StrComparator, NumComparator, UserKeyComparator>;

  template<typename T, typename Variant>
  struct variant_contains {
      static constexpr bool value = false;
  };

  template<typename T, template<typename...> class Variant, typename... Ts>
  struct variant_contains<T, Variant<Ts...>> {
      static constexpr bool value = (std::is_same_v<T, Ts> || ...);
  };

  template<typename T, typename Variant>
  static inline constexpr bool variant_contains_v =
          variant_contains<T, Variant>::value;


  class Comparator {
  public:
      ~Comparator() = default;

      template<typename Cmp>
      Comparator(Cmp cmp) : comparator_(std::move(cmp)) {
          static_assert(variant_contains_v<Cmp, ComparatorType>, "Cmp must be one of the comparator types");
          static_assert(comparator<Cmp>, "Cmp must satisfy the comparator concept");
          static_assert(!std::is_reference_v<Cmp>, "Cmp must not be a reference type");
      }

      int compare(const Slice &a, const Slice &b) const;

      void find_shortest_separator(std::string *start, const Slice &limit) const;

      void find_short_successor(std::string *key) const;

      const char *name() const;


  private:
      ComparatorType comparator_;
  };
}  // namespace LSMKV