#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>

class ThreadPool {
public:
    ThreadPool()
        : stopping(false)
    {}

    ~ThreadPool() {
        stop();
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    bool start(std::size_t threadCount) {
        if (threadCount == 0) {
            return false;
        }
        if (!workers.empty()) {
            return false;
        }

        stopping.store(false);
        workers.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back(&ThreadPool::workerLoop, this);
        }
        return true;
    }

    bool enqueue(std::function<void()> task) {
        if (!task) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping.load()) {
                return false;
            }
            tasks.push(std::move(task));
        }
        cv.notify_one();
        return true;
    }

    void stop() {
        bool wasStopping = stopping.exchange(true);
        if (wasStopping) {
            return;
        }

        cv.notify_all();

        for (std::thread& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers.clear();

        {
            std::lock_guard<std::mutex> lock(mutex);
            while (!tasks.empty()) {
                tasks.pop();
            }
        }
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this]() {
                    return stopping.load() || !tasks.empty();
                });

                if (tasks.empty()) {
                    if (stopping.load()) {
                        return;
                    }
                    continue;
                }

                task = std::move(tasks.front());
                tasks.pop();
            }

            try {
                task();
            } catch (...) {
            }
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> stopping;
};

#endif