#ifndef TIMER_H
#define TIMER_H

#include <chrono>

class Stopwatch {
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch() : start_(Clock::now()) {}

    void reset() { start_ = Clock::now(); }

    long long elapsedMs() const {
        auto d = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_);
        return d.count();
    }

private:
    Clock::time_point start_;
};

#endif