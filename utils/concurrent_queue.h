#pragma once

#include "hazard_ptr.h"

namespace alp {
  template<typename T>
  struct concurrent_queue {
      struct Node {
          virtual ~Node() = default;

          std::atomic<Node *> next{nullptr};
      };

      struct Node_Impl : public Node {
          T data;

          template<class ...Args>
          Node_Impl(Args &&...args) : data(std::forward<Args>(args)...) {}

          ~Node_Impl() override = default;
      };

      ~concurrent_queue() {
          while (pop());
          alp::hazp::retire(head_.load(std::memory_order_relaxed));
      }

      std::atomic<Node *> head_;
      std::atomic<Node *> tail_;
      std::atomic<size_t> size_;

      concurrent_queue() : head_(nullptr), tail_(nullptr), size_(0) {
          Node *node = new Node();
          head_ = tail_ = node;
      }

      template<class ...Args>
      void push(Args &&...args) {
          std::unique_ptr<Node> new_node = std::make_unique<Node_Impl>(std::forward<Args>(args)...);
          Node *tail;
          Node *next;

          alp::hazp::reserve_hazp(2);
          auto head_hp = alp::hazp::make_hazard_ptr(0);
          auto next_hp = alp::hazp::make_hazard_ptr(1);

          while (true) {
              get_node_and_next(tail_, &tail, &next, head_hp, next_hp);
              if (tail != tail_.load(std::memory_order_acquire)) continue;
              if (nullptr == next) {
                  if (tail->next.compare_exchange_strong(next, new_node.get())) break;
              } else {
                  tail_.compare_exchange_weak(tail, next);
              }
          }
          tail_.compare_exchange_weak(tail, new_node.release());
          size_.fetch_add(1, std::memory_order_relaxed);
//          size_.notify_one();
      }

      bool pop(T &value) {
          alp::hazp::reserve_hazp(2);

          auto head_hp = alp::hazp::make_hazard_ptr(0);
          auto next_hp = alp::hazp::make_hazard_ptr(1);

          Node *head;
          Node *next;
          while (true) {
              get_node_and_next(head_, &head, &next, head_hp, next_hp);

              if (head != head_.load(std::memory_order_acquire)) continue;

              if (head == tail_.load(std::memory_order_acquire)) {
                  if (next == nullptr) {
                      return false;
                  }
                  tail_.compare_exchange_weak(head, next);
              } else {
                  if (next != head->next.load(std::memory_order_acquire)) {
                      continue;
                  }

                  assert(next != nullptr);
                  if (head_.compare_exchange_strong(head, next)) {
                      value = std::move(static_cast<Node_Impl *>(next)->data);
                      alp::hazp::retire(head);
                      size_.fetch_sub(1, std::memory_order_relaxed);
                      return true;
                  }
              }
          }
      }

  private:
      void get_node_and_next(std::atomic<Node *> &atomic_node,
                             Node **node_ptr,
                             Node **next_ptr,
                             alp::hazp::hazard_ptr &node_hp,
                             alp::hazp::hazard_ptr &next_hp) {
          Node *node;
          Node *next;
          Node *temp_node;
          Node *temp_next;
          do {
              do {
                  // Make sure the node we mark still alive, otherwise
                  // it may be reclaimed before we store it.
                  temp_node = node_hp.protect(atomic_node);
                  node = atomic_node.load(std::memory_order_acquire);
              } while (temp_node != node);

              assert(temp_node == node);

              temp_next = next_hp.protect(node->next);
              next = node->next.load(std::memory_order_acquire);
          } while (temp_next != next);

          *node_ptr = node;
          *next_ptr = next;
      }

      bool pop() {
          alp::hazp::reserve_hazp(2);

          auto head_hp = alp::hazp::make_hazard_ptr(0);
          auto next_hp = alp::hazp::make_hazard_ptr(1);

          Node *head;
          Node *next;
          while (true) {
              get_node_and_next(head_, &head, &next, head_hp, next_hp);

              // Not the head node.
              if (head != head_.load(std::memory_order_acquire)) continue;

              // Empty queue.
              if (head == tail_.load(std::memory_order_acquire)) {
                  // Surely empty.
                  if (next == nullptr) {
                      return false;
                  }
                  // Not empty, try to swing tail to next.
                  tail_.compare_exchange_weak(head, next);
              } else {
                  assert(next != nullptr);
                  if (head_.compare_exchange_strong(head, next)) {
                      alp::hazp::retire(head);
                      size_.fetch_sub(1, std::memory_order_relaxed);
                      return true;
                  }
              }
          }
      }
  };

} // namespace LSMKV