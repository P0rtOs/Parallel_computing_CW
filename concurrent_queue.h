#ifndef CONCURRENT_QUEUE_H
#define CONCURRENT_QUEUE_H

#include <queue>
#include <shared_mutex>  // std::shared_mutex, std::shared_lock, std::unique_lock
#include <utility>       // std::move
#include <cstddef>       // std::size_t

// Проста потокобезпечна черга FIFO.
// Не блокується при pop — повертає false, якщо черга порожня.
template <typename T>
class ConcurrentQueue {
public:
    ConcurrentQueue() = default;

    // На всякий випадок: при знищенні просто очищаємо чергу під write-локом.
    ~ConcurrentQueue() {
        clear();
    }

    // Забороняємо копіювання / переміщення
    ConcurrentQueue(const ConcurrentQueue&)            = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;
    ConcurrentQueue(ConcurrentQueue&&)                 = delete;
    ConcurrentQueue& operator=(ConcurrentQueue&&)      = delete;

    // Перевірка, чи черга порожня (без блокування інших читачів)
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return queue_.empty();
    }

    // Поточний розмір
    std::size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return queue_.size();
    }

    // Повністю очистити чергу
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_ = std::queue<T>(); // просто створюємо нову порожню std::queue
    }

    // Забрати елемент у змінну value.
    // Повертає true, якщо щось було, false — якщо черга порожня.
    bool try_pop(T& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Версія pop без повернення значення — просто викидає front.
    bool try_pop() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        queue_.pop();
        return true;
    }

    // Додати елемент по копії
    void push(const T& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_.push(value);
    }

    // Додати елемент по move
    void push(T&& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        queue_.push(std::move(value));
    }

private:
    mutable std::shared_mutex mutex_; // read/write лок
    std::queue<T>             queue_; // звичайна черга всередині
};

#endif // CONCURRENT_QUEUE_H
