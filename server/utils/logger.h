#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <thread>

class Logger {
public:
    enum class Level {
        Debug = 0,
        Info  = 1,
        Warn  = 2,
        Error = 3
    };

    explicit Logger(std::ostream& out = std::clog)
        : out_(out)
    {}

    void setLevel(Level lvl) {
        std::lock_guard<std::mutex> g(mutex_);
        level_ = lvl;
    }

    Level level() const {
        return level_;
    }

    void log(Level lvl, const std::string& msg) {
        if (static_cast<int>(lvl) < static_cast<int>(level_)) return;

        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif

        std::ostringstream line;
        line << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        line << " [" << levelToString(lvl) << "]";
        line << " [tid=" << std::this_thread::get_id() << "]";
        line << " " << msg;

        std::lock_guard<std::mutex> g(mutex_);
        out_ << line.str() << "\n";
        out_.flush();
    }

    void debug(const std::string& msg) { log(Level::Debug, msg); }
    void info (const std::string& msg) { log(Level::Info,  msg); }
    void warn (const std::string& msg) { log(Level::Warn,  msg); }
    void error(const std::string& msg) { log(Level::Error, msg); }

private:
    static const char* levelToString(Level lvl) {
        switch (lvl) {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
        }
        return "INFO";
    }

private:
    std::ostream& out_;
    mutable std::mutex mutex_;
    Level level_ = Level::Info;
};

#endif