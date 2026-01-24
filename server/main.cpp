#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cerrno>

#include "server.h"

static void printHelp() {
    std::cout <<
R"(Usage:
  server [options]

Options:
  --ip <ip>                   Bind IP (default: 127.0.0.1)
  --port <port>               Bind port (default: 8080)
  --index-dir <path>          Directory to index (default: text_files/aclImdb)
  --threads <n>               ThreadPool size, 0=auto (default: 0)

  --scheduler-ms <ms>         Scheduler interval in milliseconds (default: 10000)
  --scheduler-sec <sec>       Scheduler interval in seconds (alternative to --scheduler-ms)
  --no-scheduler              Disable periodic indexing

  --remove-missing <0|1>      Remove missing files from index during incremental scan (default: 1)
  --clear-index <0|1>         Clear index on startup before first indexing (default: 1)

  --help                      Show this help

Examples:
  server --port 9090 --index-dir text_files/aclImdb --threads 8 --scheduler-sec 5
  server --no-scheduler --index-dir "E:\datasets\aclImdb" --port 8080
)";
}

static bool parseInt(const std::string& s, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

static bool parseBool01(const std::string& s, bool& out) {
    if (s == "1" || s == "true" || s == "TRUE") { out = true; return true; }
    if (s == "0" || s == "false" || s == "FALSE") { out = false; return true; }
    return false;
}

static bool readValue(int& i, int argc, char** argv, std::string& out) {
    std::string a = argv[i];
    auto eq = a.find('=');
    if (eq != std::string::npos) {
        out = a.substr(eq + 1);
        return true;
    }
    if (i + 1 >= argc) return false;
    out = argv[++i];
    return true;
}

int main(int argc, char** argv) {
    if (!Server::initSockets()) return 1;

    Server server;
    Server::installSignalHandlers(&server);

    Server::Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--help" || a == "-h") {
            printHelp();
            Server::cleanupSockets();
            return 0;
        }

        if (a.rfind("--ip", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --ip\n"; return 1; }
            cfg.ip = v;
            continue;
        }

        if (a.rfind("--port", 0) == 0 || a == "-p") {
            std::string v;
            if (a == "-p") {
                if (i + 1 >= argc) { std::cerr << "Missing value for -p\n"; return 1; }
                v = argv[++i];
            } else {
                if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --port\n"; return 1; }
            }
            long long p = 0;
            if (!parseInt(v, p) || p <= 0 || p > 65535) { std::cerr << "Invalid --port\n"; return 1; }
            cfg.port = static_cast<int>(p);
            continue;
        }

        if (a.rfind("--index-dir", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --index-dir\n"; return 1; }
            cfg.baseDir = std::filesystem::path(v);
            continue;
        }

        if (a.rfind("--threads", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --threads\n"; return 1; }
            long long t = 0;
            if (!parseInt(v, t) || t < 0) { std::cerr << "Invalid --threads\n"; return 1; }
            cfg.threadCount = static_cast<std::size_t>(t);
            continue;
        }

        if (a.rfind("--scheduler-ms", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --scheduler-ms\n"; return 1; }
            long long ms = 0;
            if (!parseInt(v, ms) || ms < 0) { std::cerr << "Invalid --scheduler-ms\n"; return 1; }
            cfg.schedulerInterval = std::chrono::milliseconds(ms);
            continue;
        }

        if (a.rfind("--scheduler-sec", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --scheduler-sec\n"; return 1; }
            long long sec = 0;
            if (!parseInt(v, sec) || sec < 0) { std::cerr << "Invalid --scheduler-sec\n"; return 1; }
            cfg.schedulerInterval = std::chrono::milliseconds(sec * 1000);
            continue;
        }

        if (a == "--no-scheduler") {
            cfg.enableScheduler = false;
            continue;
        }

        if (a.rfind("--remove-missing", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --remove-missing\n"; return 1; }
            bool b = true;
            if (!parseBool01(v, b)) { std::cerr << "Invalid --remove-missing (use 0/1)\n"; return 1; }
            cfg.removeMissing = b;
            continue;
        }

        if (a.rfind("--clear-index", 0) == 0) {
            std::string v;
            if (!readValue(i, argc, argv, v)) { std::cerr << "Missing value for --clear-index\n"; return 1; }
            bool b = true;
            if (!parseBool01(v, b)) { std::cerr << "Invalid --clear-index (use 0/1)\n"; return 1; }
            cfg.clearIndexOnStart = b;
            continue;
        }

        std::cerr << "Unknown arg: " << a << "\n";
        std::cerr << "Use --help\n";
        Server::cleanupSockets();
        return 1;
    }

    if (!server.initServer(cfg)) {
        Server::cleanupSockets();
        return 1;
    }

    server.acceptLoop();

    server.stop();
    Server::cleanupSockets();
    return 0;
}
