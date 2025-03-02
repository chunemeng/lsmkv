#ifndef LSMKV_EXECUTOR_H
#define LSMKV_EXECUTOR_H

#include <functional>
#include <future>
#include <thread>
#include "concurrent_queue.h"

namespace LSMKV {

  class Executor {
  private:
      void StartSchedule() {
          do {

              sem_.acquire();
              if (stop_.load(std::memory_order_acquire)) [[unlikely]] {
                  break;
              }
              task req;

              if (!fallback_queue_.pop(req)) [[unlikely]] {
                  continue;
              }

              req();
          } while (true);
      };
  public:
      using task = std::move_only_function<void()>;

      explicit Executor(int n = 1) {
          background_threads_.resize(n);
          for (int i = 0; i < n; i++) {
              background_threads_[i] = std::jthread([this] { StartSchedule(); });
          }
      }

      ~Executor() {
          if (!stop_.load(std::memory_order_acquire)) {
              Shutdown();
          }
      }

      void Shutdown() {
          stop_.store(true, std::memory_order_release);
          auto N = background_threads_.size();

          sem_.release(static_cast<long>(N));
          for (auto i = 0; i < N; i++) {
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


  private:
      std::atomic<bool> stop_{false};
      [[maybe_unused]]char pad[64];
      std::counting_semaphore<> sem_{0};
      [[maybe_unused]]char pad2[64];
      alp::concurrent_queue <task> fallback_queue_;
      /** The background thread responsible for issuing scheduled requests to the disk manager. */
      std::vector<std::jthread> background_threads_;
  };

}// namespace LSMKV

#endif//LSMKV_EXECUTOR_H
