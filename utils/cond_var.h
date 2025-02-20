#pragma once

#include <condition_variable>
#include "rwlock.h"

namespace LSMKV {

  template<typename Lock>
  concept Mutex = requires(Lock l) {
      { l.lock() } -> std::same_as<void>;
      { l.unlock() } -> std::same_as<void>;
  };


  // A condition variable that works with a RWLock.
  // I know there is std::condition_variable_any, but any is implemented with an inner mutex.
  // (although this performance may be worse than std::condition_variable_any)
  class CondVar {
  public:

      explicit CondVar(RWLock *lock) : lock_(lock) {}

      CondVar() = delete;

      void Wait() {
          lock_->before_wait();
          std::unique_lock<std::mutex> ul(*lock_->get_mutex(), std::adopt_lock);
          cv_.wait(ul);
          ul.release();
          lock_->after_wait();
      }

      template<typename Predicate>
      void Wait(Predicate pred) {
          lock_->before_wait();
          std::unique_lock<std::mutex> ul(*lock_->get_mutex(), std::adopt_lock);
          cv_.wait(ul, pred);
          ul.release();
          lock_->after_wait();
      }

      void NotifyOne() {
          cv_.notify_one();
      }

      void NotifyAll() {
          cv_.notify_all();
      }


  private:
      std::condition_variable cv_;

      RWLock *const lock_;
  };

} // namespace LSMKV