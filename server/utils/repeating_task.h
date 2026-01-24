#ifndef REPEATING_TASK_H
#define REPEATING_TASK_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <string>

#include "utils/logger.h"

class RepeatingTask {
public:
    RepeatingTask() = default;

    RepeatingTask(const RepeatingTask&)            = delete;
    RepeatingTask& operator=(const RepeatingTask&) = delete;
    RepeatingTask(RepeatingTask&&)                 = delete;
    RepeatingTask& operator=(RepeatingTask&&)      = delete;

    ~RepeatingTask() {
        stop();
    }

    bool start(std::function<void()> task,
               std::chrono::milliseconds interval,
               Logger* logger = nullptr,
               std::string name = "repeating-task") {
        if (!task) return false;
        if (running_.exchange(true)) return false;

        task_     = std::move(task);
        interval_ = interval;
        logger_   = logger;
        name_     = std::move(name);
        stopRequested_.store(false);

        worker_ = std::thread([this]() { loop_(); });

        if (logger_) {
            logger_->info(name_ + " started, intervalMs=" + std::to_string(interval_.count()));
        }
        return true;
    }

    void requestStop() {
        stopRequested_.store(true);
        cv_.notify_all();
    }

    void stop() {
        if (!running_.load()) return;
        requestStop();
        if (worker_.joinable()) {
            worker_.join();
        }
        running_.store(false);
    }

    bool isRunning() const {
        return running_.load();
    }

private:
    void loop_() {
        while (!stopRequested_.load()) {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait_for(lk, interval_, [this]() { return stopRequested_.load(); });
            if (stopRequested_.load()) break;
            lk.unlock();

            try {
                task_();
            } catch (...) {
                if (logger_) {
                    logger_->error(name_ + " failed: unknown exception");
                }
            }
        }
        if (logger_) {
            logger_->info(name_ + " stopped");
        }
    }

private:
    std::thread worker_;
    std::function<void()> task_;
    std::chrono::milliseconds interval_{std::chrono::seconds(10)};

    std::mutex mutex_;
    std::condition_variable cv_;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};

    Logger* logger_{nullptr};
    std::string name_{"repeating-task"};
};

#endif