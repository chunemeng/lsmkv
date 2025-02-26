#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <cassert>

namespace alp::hazp {

  struct holder {
      std::atomic<const void *> ptr{nullptr};
      std::atomic<uint8_t> active{0};
  };

  class hazard_ptr {
  private:
      holder *holder_;

  public:
      hazard_ptr(holder *holder) : holder_(holder) {}

      hazard_ptr() = delete;

      ~hazard_ptr() {
          unmark();
      }

      template<class T>
      T *protect(std::atomic<T *> &ptr) {
          T *p = ptr.load(std::memory_order_acquire);
          holder_->ptr.store(p, std::memory_order_release);
          return p;
      }

      void reset() {
          unmark();
      }

  private:
      void unmark() {
          holder_->ptr.store(nullptr, std::memory_order_release);
      }
  };

/**
 *  linked_list
 *
 *  Template parameter Node must support set_next
 *
 */
  template<typename Node>
  class linked_list {
      Node *head_;
      Node *tail_;
      int size_;

  public:
      linked_list() noexcept: head_(nullptr), tail_(nullptr), size_(0) {}

      explicit linked_list(Node *head, Node *tail) noexcept
              : head_(head), tail_(tail), size_(0) {}

      Node *head() const noexcept { return head_; }

      Node *tail() const noexcept { return tail_; }

      int count() const noexcept { return size_; }

      bool empty() const noexcept { return head() == nullptr; }

      void push(Node *node) noexcept {
          node->set_next(nullptr);
          if (tail_) {
              tail_->set_next(node);
          } else {
              head_ = node;
          }
          size_++;
          tail_ = node;
      }

      void splice(linked_list &l) {
          if (l.empty()) {
              return;
          }

          if (head() == nullptr) {
              head_ = l.head();
              size_ = l.count();
          } else {
              size_ += l.count();
              tail_->set_next(l.head());
          }
          tail_ = l.tail();
          l.clear();
      }

      void clear() {
          size_ = 0;
          head_ = nullptr;
          tail_ = nullptr;
      }

  }; // linked_list

  template<typename Node>
  class shared_head_only_list {
      std::atomic<uintptr_t> head_{0};

  public:
      void push(Node *node) noexcept {
          if (node == nullptr) {
              return;
          }
          auto oldval = head();
          auto newval = reinterpret_cast<uintptr_t>(node);

          while (true) {
              auto ptrval = oldval;

              auto ptr = reinterpret_cast<Node *>(ptrval);
              node->set_next(ptr);
              if (cas_head(oldval, newval)) {
                  break;
              }
          }
      }

      void push_list(linked_list<Node> &l) noexcept {
          if (l.empty()) [[unlikely]] {
              return;
          }
          auto oldval = head();

          while (true) {
              auto newval = reinterpret_cast<uintptr_t>(l.head());

              auto ptrval = oldval;

              auto ptr = reinterpret_cast<Node *>(ptrval);
              l.tail()->set_next(ptr);
              if (cas_head(oldval, newval)) {
                  break;
              }
          }
      }

      Node *pop_all() noexcept {
          return pop_all_no_lock();
      }

      bool empty() const noexcept { return head() == 0u; }

  private:
      uintptr_t head() const noexcept {
          return head_.load(std::memory_order_acquire);
      }

      uintptr_t exchange_head() noexcept {
          auto newval = reinterpret_cast<uintptr_t>(nullptr);
          auto oldval = head_.exchange(newval, std::memory_order_acq_rel);
          return oldval;
      }

      bool cas_head(uintptr_t &oldval, uintptr_t newval) noexcept {
          return head_.compare_exchange_weak(
                  oldval, newval, std::memory_order_acq_rel, std::memory_order_acquire);
      }

      Node *pop_all_no_lock() noexcept {
          auto oldval = exchange_head();
          return reinterpret_cast<Node *>(oldval);
      }

  }; // shared_head_only_list

  class reclaimer {
  private:
      reclaimer() = default;

      ~reclaimer() {
          reclaim_all_objects();
      }

      reclaimer(const reclaimer &) = delete;

      reclaimer &operator=(const reclaimer &) = delete;

      class local_holder final {
      private:
          local_holder() = default;

          ~local_holder() {
              for (auto &node: holder_storage) {
                  node->ptr.store(nullptr);
                  node->active.store(false);
              }
          }

          local_holder(const local_holder &) = delete;

          local_holder &operator=(const local_holder &) = delete;

      public:
          template<typename T>
          hazard_ptr make_hazard_ptr(std::atomic<T *> &ptr) {
              for (auto i: holder_storage) {
                  if (i->ptr.load(std::memory_order_release) == nullptr) {
                      i->ptr.store(ptr.load(std::memory_order_acquire), std::memory_order_release);
                      return {i};
                  }
              }
              auto holder = instance().make_holder();
              holder->ptr.store(ptr.load(std::memory_order_acquire), std::memory_order_release);
              holder_storage.push_back(holder);
              return {holder};
          }

          hazard_ptr get_hazard_ptr(uint8_t index) {
              return {holder_storage[index]};
          }

          void reserve_hazp(uint8_t size) {
              auto old_size = holder_storage.size();

              if (old_size >= size) [[likely]] {
                  return;
              }

              holder_storage.resize(size);
              for (auto i = old_size; i < size; ++i) {
                  holder_storage[i] = instance().make_holder();
              }
          }

          static local_holder &get_instance(reclaimer *reclaimer) {
              static thread_local local_holder instance;
              return instance;
          }

          std::vector<holder *> holder_storage;
      };

      struct retire_node {
          virtual const void *raw_ptr() const { return nullptr; }

          virtual ~retire_node() = default;

          void set_next(retire_node *next) { next_ = next; }

          retire_node *next() const { return next_; }

          retire_node *next_{nullptr};
      };

      struct holder_list {
          holder_list() : head_(new holder_list_node()), tail_(head_.load()) {
          }

          ~holder_list() {
              auto node = head_.load()->next.load();
              while (node) {
                  auto next = node->next.load();

                  while (node->holder->active.load()) {
                      std::this_thread::yield();
                  }

                  delete node;

                  node = next;
              }
              delete head_.load();
          }

          struct holder_list_node {
              using holder_t = alp::hazp::holder;

              holder_list_node() = default;

              holder_list_node(holder_t *holder, holder_list_node *next) : holder(holder), next(next) {}

              ~holder_list_node() {
                  delete holder;
              }

              holder_t *holder{nullptr};
              std::atomic<holder_list_node *> next{nullptr};
          };

          void push_back(holder *holder) {
              auto new_node = new holder_list_node(holder, nullptr);
              auto tail = tail_.load(std::memory_order_acquire);

              while (!tail_.compare_exchange_weak(tail, new_node)) {
              }

              tail->next.store(new_node, std::memory_order_release);
          }

          std::atomic<holder_list_node *> head_;

          std::atomic<holder_list_node *> tail_;
      };

      using retired_list = shared_head_only_list<retire_node>;
      using list = linked_list<retire_node>;

      constexpr static int kNumShards = 8;

      static constexpr int kShardMask = kNumShards - 1;

  public:
      static reclaimer &instance() {
          static reclaimer instance;
          return instance;
      }

      void reserve_hazp(uint8_t size) {
          return get_instance().reserve_hazp(size);
      }

      void release_holders() {
          auto &holders = get_instance().holder_storage;
          for (auto &holder: holders) {
              holder->ptr.store(nullptr);
              holder->active.store(false);
          }

          holders.clear();
      }

      hazard_ptr make_hazard_ptr(uint8_t index) {
          return get_instance().get_hazard_ptr(index);
      }

      template<typename T, typename D = std::default_delete<T>>
      void retire(T *ptr, D deleter = {}) {
          struct retire_node_impl : public retire_node {
              explicit retire_node_impl(T *ptr, D d) : ptr_(ptr, d) {}

              const void *raw_ptr() const override { return ptr_.get(); }

              ~retire_node_impl() override = default;

              std::unique_ptr<T, D> ptr_;
          };

          auto retired = new retire_node_impl(ptr, deleter);
          push_list(retired);
      }

      void reclaim() {
          do_reclamation(0);
      }

  private:
      holder *make_holder() {
          auto holder = new ::alp::hazp::holder();
          holder->active.store(true, std::memory_order_release);

          holder_list_.push_back(holder);
          return holder;
      }

      local_holder &get_instance() {
          return local_holder::get_instance(this);
      }

      void reclaim_all_objects() {
          for (auto &s: retired_list_) {
              retire_node *head = s.pop_all();
              reclaim_obj(head);
          }
      }


      void check_threshold_and_reclaim() {
          int rcount = check_count_threshold();
          if (rcount == 0) {
              return;
          }

          do_reclamation(rcount);
      }

      template<typename Cond>
      void list_match_condition(
              retire_node *obj, list &match, list &nomatch, const Cond &cond) {
          while (obj) {
              auto next = obj->next();
              assert(obj != next);
              if (cond(obj)) {
                  match.push(obj);
              } else {
                  nomatch.push(obj);
              }
              obj = next;
          }
      }

      bool retired_list_empty() {
          // retired_list_ is std::array<retired_list, kNumShards>
          return std::ranges::all_of(retired_list_, [](auto &s) { return s.empty(); });
      }

      void reclaim_obj(retire_node *obj) {
          while (obj) {
              auto next = obj->next();
              delete obj;
              obj = next;
          }
      }

      using Set = std::unordered_set<const void *>;

      int match_reclaim(retire_node *retired[], Set &hs, bool &done) {
          done = true;
          list not_reclaimed;
          int count = 0;
          for (int s = 0; s < kNumShards; ++s) {
              list match, nomatch;
              list_match_condition(retired[s], match, nomatch, [&](retire_node *o) {
                  return hs.count(o->raw_ptr()) > 0;
              });
              count += nomatch.count();

              reclaim_obj(nomatch.head());
              if (!retired_list_empty()) {
                  done = false;
              }
              not_reclaimed.splice(match);
          }
          list l(not_reclaimed.head(), not_reclaimed.tail());
          retired_list_[0].push_list(l);
          return count;
      }

      void do_reclamation(int rcount) {
          assert(rcount >= 0);
          while (true) {
              retire_node *untagged[kNumShards];
              bool done = true;
              if (extract_retired_objects(untagged)) {
                  std::atomic_thread_fence(std::memory_order_seq_cst);
                  Set hs = load_hazptr_vals();
                  rcount -= match_reclaim(untagged, hs, done);
              }
              if (rcount) {
                  add_count(rcount);
              }
              rcount = check_count_threshold();
              if (rcount == 0 && done)
                  break;
          }
      }

      bool extract_retired_objects(retire_node *list[]) {
          bool empty = true;
          for (int s = 0; s < kNumShards; ++s) {
              list[s] = retired_list_[s].pop_all();
              if (list[s]) {
                  empty = false;
              }
          }
          return !empty;
      }

      int check_count_threshold() {
          int rcount = load_count();
          while (rcount >= 1000) {
              if (cas_count(rcount, 0)) {
                  return rcount;
              }
          }
          return 0;
      }

      [[nodiscard]] Set load_hazptr_vals() const {
          auto head = holder_list_.head_.load(std::memory_order_acquire);
          Set protected_ptrs;

          if (holder_list_.head_ != nullptr) {
              auto node = head->next.load(std::memory_order_acquire);

              while (node) {
                  protected_ptrs.insert(node->holder->ptr.load(std::memory_order_acquire));
                  node = node->next.load(std::memory_order_acquire);
              }
          }
          return protected_ptrs;
      }

      int add_count(int rcount) {
          return count_.fetch_add(rcount, std::memory_order_release);
      }

      int load_count() {
          return count_.load(std::memory_order_acquire);
      }

      bool cas_count(int &expected, int desired) {
          return count_.compare_exchange_strong(expected, desired, std::memory_order_relaxed);
      }

      void push_list(retire_node *ptr) {
          // NOTE: 4 bit may already skip the empty bits.
          auto hash = reinterpret_cast<uintptr_t>(ptr) >> 8;
          retired_list_[hash & kShardMask].push(ptr);
          add_count(1);

          check_threshold_and_reclaim();
      }

      holder_list holder_list_;

      std::atomic<int> count_{0};

      retired_list retired_list_[kNumShards];
  };

  static inline void reserve_hazp(uint8_t size) {
      reclaimer::instance().reserve_hazp(size);
  }

  static inline hazard_ptr make_hazard_ptr(uint8_t index) {
      return reclaimer::instance().make_hazard_ptr(index);
  }

  template<typename T, typename D = std::default_delete<T>>
  static inline void retire(T *ptr, D deleter = {}) {
      reclaimer::instance().retire(ptr, deleter);
  }

} // namespace alp::reclaimer

