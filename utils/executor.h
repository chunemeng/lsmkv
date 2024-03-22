#ifndef LSMKV_EXECUTOR_H
#define LSMKV_EXECUTOR_H

#include <functional>
#include <future>
#include <thread>

namespace LSMKV {

  template<int N = 1>
  class Executor {
  public:
      using task = std::move_only_function<void()>;

      Executor() {
          for (int i = 0; i < N; i++) {
              background_threads_[i] = std::jthread([this] { StartSchedule(); });
          }
      }

      ~Executor() {
          stop_ = true;
          sem_.release(N);
          for (int i = 0; i < N; i++) {
              background_threads_[i].join();
          }
      }

      template<typename Fun, typename Ret = std::invoke_result_t<std::decay_t<Fun>>>
      std::future<Ret> submit(Fun &&fun) {
          std::promise<Ret> p;
          auto future = p.get_future();
          task ts = [f = std::forward<Fun>(fun), promise = std::move(p)]() mutable {
              if constexpr (std::is_void_v<Ret>) {
                  f();
                  promise.set_value();
              } else {
                  promise.set_value(f());
              }
          };
          fallback_queue_.push(std::move(ts));
          sem_.release();
          return future;
      }

      void StartSchedule() {
          do {
              sem_.acquire();
              if (stop_) [[unlikely]] {
                  break;
              }

              task req = std::move(fallback_queue_.front());
              fallback_queue_.pop();

              req();
          } while (true);
      };

  private:
      std::atomic<bool> stop_{false};
      std::counting_semaphore<> sem_{0};
//      Queue<task> fallback_queue_;

      std::queue<task> fallback_queue_;
      /** The background thread responsible for issuing scheduled requests to the disk manager. */
      std::array<std::jthread, N> background_threads_;
  };

  static inline Executor<> &default_scheduler() {
      static Executor<> scheduler;
      return scheduler;
  }


}// namespace LSMKV

#endif//LSMKV_EXECUTOR_H
