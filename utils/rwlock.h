#pragma once

#include <mutex>
#include <atomic>
#include <condition_variable>
#include "log.h"

namespace LSMKV {
  // this function name is to be consistent with std::xxx_lock
  class RWLock {
  private:
      void shared_lock_wait() {
          mtx_.lock();
          readers_.fetch_add(WAITER, std::memory_order_acquire);
          mtx_.unlock();
      }

      bool shared_lock_inner() {
          uint32_t lk = 0;
          while (!readers_.compare_exchange_weak(lk, lk + WAITER,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
              if (lk & X)
                  return false;
          }
          return true;
      }

      bool shared_unlock_inner() {
          uint32_t lk = readers_.fetch_sub(WAITER, std::memory_order_release);
          assert(~X & lk);
          return lk == X + WAITER;
      }

      uint32_t lock_inner() {
          return readers_.fetch_add(X, std::memory_order_acquire);
      }

      void lock_inner_wait(uint32_t lk) noexcept {
          lk |= X;
          do {
              readers_.wait(lk);
              lk = readers_.load(std::memory_order_acquire);
          } while (lk != X);
      }

      void unlock_inner() {
          readers_.store(0, std::memory_order_release);
      }

  public:
      RWLock() = default;

      RWLock(const RWLock &) = delete;

      RWLock &operator=(const RWLock &) = delete;


      std::mutex *get_mutex() {
          return std::addressof(mtx_);
      }

      void lock_shared() {
          if (!shared_lock_inner()) {
              shared_lock_wait();
          }
      }

      void unlock_shared() {
          bool notify = shared_unlock_inner();
          if (notify) {
              readers_.notify_one();
          }
      }

      void before_wait() {
          // at this point, must hold mtx_
          writer_waiting_ = true;

          unlock_inner();
          // then can wait in cond var
      }

      // the two functions below are for atomic_wait
      // this will unlock this mutex, allow other threads to acquire the read lock
      // still block the other write lock
      template<typename AtomicWait>
      void atomic_wait(AtomicWait &&atomic_wait) {
          before_wait();
          mtx_.unlock();
          atomic_wait();
          mtx_.lock();
          after_wait();
      }

      template<typename T>
      void atomic_wait(std::atomic<T> &atomic, T &&value) {
          if (atomic.load(std::memory_order_acquire) == value) {
              return;
          }

          before_wait();
          mtx_.unlock();
          atomic.wait(value, std::memory_order_acquire);
          mtx_.lock();
          after_wait();
      }


      void lock() {
          mtx_.lock();
          if (writer_waiting_) {
              std::unique_lock<std::mutex> ul(mtx_, std::adopt_lock);
              writer_waiting_signal_.wait(ul, [this] { return !writer_waiting_; });
              ul.release();
          }

          if (auto lk = lock_inner()) {
              lock_inner_wait(lk);
          }
      }

      void unlock() {
          unlock_inner();

          bool need_notify = writer_waiting_;
          if (writer_waiting_) {
              writer_waiting_ = false;
          }

          mtx_.unlock();

          if (need_notify) {
              writer_waiting_signal_.notify_all();
          }


      }

      bool try_upgrade_to_writer() {
          if (mtx_.try_lock()) {
              unlock_shared();

              if (auto lk = lock_inner()) {
                  lock_inner_wait(lk);
              }

          }
          return false;
      }

      void after_wait() {
          // at this point, must hold mtx_
          assert(writer_waiting_ == true);

//          move below to unlock function, because mutex is held now
//          writer_waiting_ = false;
//          writer_waiting_signal_.notify_one();

          if (auto lk = lock_inner()) {
              lock_inner_wait(lk);
          }

      }

  private:
      std::mutex mtx_;
      std::atomic<uint32_t> readers_{0};
      bool writer_waiting_{false};
      std::condition_variable writer_waiting_signal_;

      static constexpr uint32_t X = 0x80000000u;
      static constexpr uint32_t WAITER = 1;
  };
} // namespace LSMKV
