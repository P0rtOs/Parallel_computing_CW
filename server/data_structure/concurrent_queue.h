#ifndef CONCURRENT_QUEUE_H
#define CONCURRENT_QUEUE_H

#include <queue>
#include <shared_mutex>
#include <utility>
#include <cstddef>

template <typename T>
class ConcurrentQueue {
public:
    ConcurrentQueue() = default;

    ~ConcurrentQueue() {
        clear();
    }

    ConcurrentQueue(const ConcurrentQueue&)            = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;
    ConcurrentQueue(ConcurrentQueue&&)                 = delete;
    ConcurrentQueue& operator=(ConcurrentQueue&&)      = delete;

    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return queue_.size();
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_ = std::queue<T>();
    }

    bool try_pop(T& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool try_pop() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        queue_.pop();
        return true;
    }

    void push(const T& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_.push(value);
    }

    void push(T&& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_.push(std::move(value));
    }

private:
    mutable std::shared_mutex mutex_;
    std::queue<T>             queue_;
};

#endif
