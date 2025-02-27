#pragma once

#include <utility>
#include <string>
#include <cassert>
#include <type_traits>
#include <variant>

namespace LSMKV {
  class Slice;

  namespace detail {
    template<typename T>
    concept comparator = requires(T t){
        { t.compare_impl(std::declval<const Slice &>(), std::declval<const Slice &>()) } -> std::convertible_to<int>;
        {
        t.find_shortest_separator_impl(std::declval<std::string *>(), std::declval<const Slice &>())
        } -> std::convertible_to<void>;
        { t.find_short_successor_impl(std::declval<std::string *>()) } -> std::convertible_to<void>;
        { t.name_impl() } -> std::convertible_to<const char *>;
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
  }


  class InternalKeyComparator : public detail::ComparatorImpl<InternalKeyComparator> {
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


  class StrComparator : public detail::ComparatorImpl<StrComparator> {
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

  class NumComparator : public detail::ComparatorImpl<NumComparator> {
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

  struct UserKeyComparator : public detail::ComparatorImpl<UserKeyComparator> {
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

  class UserDefinedComparator : public detail::ComparatorImpl<UserDefinedComparator> {
  public:
      virtual ~UserDefinedComparator() = default;

      virtual int compare_impl(const Slice &a, const Slice &b) const = 0;

      virtual void find_shortest_separator_impl(std::string *start, const Slice &limit) const = 0;

      virtual void find_short_successor_impl(std::string *key) const = 0;

      virtual const char *name_impl() const = 0;
  };


  template<typename VirtualC, typename ...T>
  struct BuildComparatorType {
      using type = std::variant<detail::ComparatorImpl<T>..., VirtualC>;
  };


  namespace detail {


    class AnyComparator {
    public:
        ~AnyComparator() = default;

        AnyComparator(UserDefinedComparator *comparator);

        AnyComparator(ComparatorImpl<UserDefinedComparator> *comparator) : comparator_(comparator) {}

        int compare(const Slice &a, const Slice &b) const {
            return comparator_->compare(a, b);
        }

        void find_shortest_separator(std::string *start, const Slice &limit) const {
            comparator_->find_shortest_separator(start, limit);
        }

        void find_short_successor(std::string *key) const {
            comparator_->find_short_successor(key);
        }

        const char *name() const {
            return comparator_->name();
        }

    private:
        ComparatorImpl<UserDefinedComparator> *comparator_;
    };


    using ComparatorType = BuildComparatorType<detail::AnyComparator, InternalKeyComparator, StrComparator, NumComparator, UserKeyComparator>::type;


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
  }


  class Comparator {
  public:
      ~Comparator() = default;

      template<typename Cmp>
      Comparator(Cmp&& cmp) : comparator_(std::forward<Cmp>(cmp)) {
          static_assert(
                  !std::is_function_v<std::remove_pointer_t<Cmp>>,
                  "Function types are not allowed as comparators"
          );
          if constexpr (std::is_pointer_v<Cmp>) {
              static_assert(!std::is_pointer_v<std::remove_pointer_t<Cmp>>, "Cmp must not be T**");
              static_assert(
                      std::is_base_of_v<UserDefinedComparator, std::remove_pointer_t<Cmp>> ||
                      std::is_base_of_v<detail::ComparatorImpl<UserDefinedComparator>, std::remove_pointer_t<Cmp>>,
                      "Cmp must be one of the comparator types");

          } else {
              static_assert(
                      detail::variant_contains_v<Cmp, detail::ComparatorType> ||
                      detail::variant_contains_v<detail::ComparatorImpl<Cmp>, detail::ComparatorType>,
                      "Cmp must be one of the comparator types");
              static_assert(!std::is_reference_v<Cmp>, "Cmp must not be a reference type");
          }


      }

      int compare(const Slice &a, const Slice &b) const;

      void find_shortest_separator(std::string *start, const Slice &limit) const;

      void find_short_successor(std::string *key) const;

      const char *name() const;

  private:
      detail::ComparatorType comparator_;
  };
}  // namespace LSMKV