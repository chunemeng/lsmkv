#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "utils/arena.h"
#include <cstdint>
#include <ctime>
#include <random>
#include <bit>
#include <atomic>
#include "utils/slice.h"
#include "utils/comparator.h"

namespace LSMKV {
  static constexpr int MAX_LEVEL = 16;

  template<typename K, typename Cmp>
  concept KeyComparator = requires(const K &a, const K &b) {
      { Cmp::compare(a, b) } -> std::same_as<int>;
  };

  template<typename K, typename Cmp>
  class Skiplist {
  private:
      using byte = uint8_t;

      static_assert(KeyComparator<K, Cmp>, "Cmp must be a KeyComparator");

      struct node {
          K const _key;
          std::atomic<node *> _next[1];

          node *next(int n) {
              assert(n >= 0);
              // Use an 'acquire load' so that we observe a fully initialized
              // version of the returned Node.
              return _next[n].load(std::memory_order_acquire);
          }

          void setnext(int n, node *x) {
              assert(n >= 0);
              // Use a 'release store' so that anybody who reads through this
              // pointer observes a fully initialized version of the inserted node.
              _next[n].store(x, std::memory_order_release);
          }

          // No-barrier variants that can be safely used in a few locations.
          node *NoBarrier_Next(int n) {
              assert(n >= 0);
              return _next[n].load(std::memory_order_relaxed);
          }

          void NoBarrier_SetNext(int n, node *x) {
              assert(n >= 0);
              _next[n].store(x, std::memory_order_relaxed);
          }

          node(K &&key) : _key(std::move(key)) {
          }

          node(const K &key) : _key(key) {
          }

          ~node() = default;
      };


      bool KeyIsAfterNode(const K &_key, node *n) const {
          return (n != nullptr) && Cmp::compare(n->_key, _key) < 0;
      }


      bool key_equal(const K &_key, node *n) const {
          return Cmp::compare(n->_key, _key) == 0;
      }

      // return the node ahead of node
      node *findNode(const K &_key) const {
          node *cur = _head;
          byte level = getMaxHeight() - 1;
          while (true) {
              node *next = cur->next(level);
              if (KeyIsAfterNode(_key, next)) {
                  cur = next;
              } else {
                  if (equal(_key, next)) {
                      return next;
                  }
                  if (level == 0) {
                      return next;
                  } else {
                      level--;
                  }
              }
          }
      }

      node *find_set_prev(const K &key, node **prev) {
          node *cur = _head;
          byte level = getMaxHeight() - 1;
          while (true) {
              node *next = cur->next(level);
              if (KeyIsAfterNode(key, next)) {
                  cur = next;
              } else {
                  prev[level] = cur;
                  if (level == 0) {
                      return next;
                  } else {
                      level--;
                  }
              }
          }
      }

      node *findHelper(const K &key, node **prev) {
          node *cur = _head;
          byte level = getMaxHeight() - 1;
          while (true) {
              node *next = cur->next(level);
              if (KeyIsAfterNode(key, next)) {
                  cur = next;
              } else {
                  if (equal(key, next)) {
                      return next;
                  }
                  if (prev != nullptr) prev[level] = cur;
                  if (level == 0) {
                      return next;
                  } else {
                      level--;
                  }
              }
          }
      }

      byte random_level() {
          // Twice faster than upper
          return MAX_LEVEL - std::bit_width((uint32_t) (((1 << (MAX_LEVEL - 1)) - 1) & rnd()));
      }

      [[nodiscard]] inline byte getMaxHeight() const noexcept {
          return max_level;
      }


      template<typename U>
      node *createNode(U &&key, const byte &level) {
          auto node_memory = arena->allocateAligned(sizeof(node) + sizeof(std::atomic<node *>) * (level - 1));
          return new(node_memory) node(std::forward<U>(key));
      }

      std::minstd_rand rnd{std::random_device{}()};
      Arena *arena;
      std::atomic<byte> max_level;
      node *_head;
  public:
      class Iterator {
      public:
          explicit Iterator(const Skiplist *list) : _list(list), _cur(nullptr), _end(nullptr) {};

          ~Iterator() = default;

          [[nodiscard]] bool hasNext() const {
              return _end != _cur && _cur != nullptr;
          }

          const K &key() const {
              return _cur->_key;
          }

          void next() {
              _cur = _cur->next(0);
          }

          void seek(const Slice &key) {
              _cur = _list->findNode(key);
          }

          void seek(const Slice &K1, const Slice &K2) {
              _cur = _list->findNode(K1);
              _end = _list->findNode(K2);
              if (_end != nullptr && _end->_key == K2) _end = _end->next(0);
          }

          void seekToFirst() {
              _cur = _list->_head->next(0);
          }

      private:
          // not include _end!!!!
          node *_cur;
          node *_end;
          const Skiplist *_list;
      };

      explicit Skiplist(Arena *arena)
              : arena(arena), _head(createNode(K{}, MAX_LEVEL)), max_level(1) {
          for (byte level = 0; level < MAX_LEVEL; level++) {
              _head->setnext(level, nullptr);
          }
      };

      ~Skiplist() = default;

      template<typename U>
      void insert(U &&key) {
          node *prev[MAX_LEVEL];
          node *n = find_set_prev(key, prev);

          assert(!equal(key, n));

          byte height = random_level();
          if (height > getMaxHeight()) {
              for (byte i = getMaxHeight(); i < height; i++) {
                  prev[i] = _head;
              }
              max_level = height;
          }

          n = createNode(std::forward<U>(key), height);
          for (int i = 0; i < height; i++) {
              n->NoBarrier_SetNext(i, prev[i]->NoBarrier_Next(i));
              prev[i]->setnext(i, n);
          }
      }

      bool equal(const K &_key, node *n) const {
          return (n != nullptr) && Cmp::compare(n->_key, _key) == 0;
      }

      bool contains(const K &key) const {
          auto cur = findNode(key);
          return cur && cur->_key == key;
      }

      Skiplist<K, Cmp> &operator=(const Skiplist<K, Cmp> &other) = delete;

      Skiplist(const Skiplist<K, Cmp> &other) = delete;
  };
} // namespace Skiplist

#endif // SKIPLIST_H
