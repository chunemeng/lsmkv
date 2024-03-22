#ifndef QUEUE_H
#define QUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>

namespace LSMKV {
    template<typename T>
    class Queue {
    public:
        Queue() = default;

        ~Queue() = default;

        Queue(Queue<T> &&other) noexcept {
            if (this == &other) {
                return;
            }
            queue_ = std::move(other.queue_);
        }

        Queue<T> &operator=(Queue &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            queue_ = std::move(other.queue_);
        }

        Queue(const Queue<T> &other) = delete;

        Queue<T> &operator=(const Queue &other) = delete;

        template<class F>
        void push(F &&value) {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(std::forward<F>(value));
            lock.unlock();
            empty_.notify_one();
        }

        T pop() {
            std::unique_lock<std::mutex> lock(mutex_);
            empty_.wait(lock, [&]() { return !queue_.empty(); });
            T value = std::move(queue_.front());
            queue_.pop();
            lock.unlock();
            return value;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable empty_;
        std::queue<T> queue_;
    };
};

#endif //QUEUE_H
