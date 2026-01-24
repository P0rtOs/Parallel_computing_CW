#ifndef SIMPLE_SERVER_H
#define SIMPLE_SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cstdint>
#include <atomic>
#include <cctype>
#include <csignal>
#include <chrono>

#include "index_manager.h"
#include "data_structure/thread_pool.h"
#include "utils/logger.h"
#include "utils/timer.h"
#include "utils/repeating_task.h"
#include "utils/response_builder.h"
#include "constants/commands.h"

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "Ws2_32.lib")

    #ifdef INDEX_ALL
    #undef INDEX_ALL
    #endif
    #ifdef ADD_FILE
    #undef ADD_FILE
    #endif
    #ifdef REMOVE_FILE
    #undef REMOVE_FILE
    #endif
    #ifdef REINDEX_FILE
    #undef REINDEX_FILE
    #endif
    #ifdef HAS_FILE
    #undef HAS_FILE
    #endif
    #ifdef INDEX_DIR
    #undef INDEX_DIR
    #endif
    #ifdef REINDEX_DIR
    #undef REINDEX_DIR
    #endif
    #ifdef REBUILD_INDEX
    #undef REBUILD_INDEX
    #endif
    #ifdef SEARCH_ONE
    #undef SEARCH_ONE
    #endif
    #ifdef SEARCH_ALL
    #undef SEARCH_ALL
    #endif
    #ifdef SEARCH_ANY
    #undef SEARCH_ANY
    #endif
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
#endif

class Server {
public:
    struct Config {
        std::string ip = "127.0.0.1";
        int         port = 8080;
        std::filesystem::path baseDir = std::filesystem::path("text_files") / "aclImdb";

        bool enableScheduler = true;
        std::chrono::milliseconds schedulerInterval{ std::chrono::seconds(10) };

        std::size_t threadCount = 0;
        bool removeMissing = true;
        bool clearIndexOnStart = true;
    };

    Server()
        : listenSocket(INVALID_SOCKET),
          stopRequested(false),
          threadPoolStarted(false)
    {}

    ~Server() {
        stop();
    }

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = delete;
    Server& operator=(Server&&)      = delete;

    static bool initSockets() {
    #ifdef _WIN32
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << "\n";
            return false;
        }
    #endif
        return true;
    }

    static void cleanupSockets() {
    #ifdef _WIN32
        WSACleanup();
    #endif
    }

    // встановлює глобальний хендлер, який:
    // - ставить stopRequested
    // - закриває listen socket
    static void installSignalHandlers(Server* srv) {
        instanceForSignals = srv;
#ifdef _WIN32
        SetConsoleCtrlHandler(&Server::consoleCtrlHandler, TRUE);
#else
        std::signal(SIGINT,  &Server::posixSignalHandler);
        std::signal(SIGTERM, &Server::posixSignalHandler);
#endif
    }

    void requestStop() {
        stopRequested.store(true);
        indexScheduler.requestStop();
        if (listenSocket != INVALID_SOCKET) {
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
        }
    }

    Logger& getLogger() { return logger; }

        bool initServer(const std::string& ip, int port) {
        Config cfg;
        cfg.ip = ip;
        cfg.port = port;
        return initServer(cfg);
    }

    bool initServer(const Config& cfg) {
    #ifdef _WIN32
        listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    #else
        listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    #endif
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "socket() failed\n";
            return false;
        }

        int opt = 1;
        if (setsockopt(listenSocket,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       reinterpret_cast<char*>(&opt),
                       sizeof(opt)) == SOCKET_ERROR) {
            std::cerr << "setsockopt() failed\n";
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return false;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<std::uint16_t>(cfg.port));
        addr.sin_addr.s_addr = inet_addr(cfg.ip.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            std::cerr << "inet_addr() failed for ip: " << cfg.ip << "\n";
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return false;
        }

        if (bind(listenSocket,
                 reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "bind() failed\n";
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "listen() failed\n";
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return false;
        }

        baseDir = cfg.baseDir;
        {
            indexManager.setLogger(&logger);

            indexThreads = cfg.threadCount;
            if (indexThreads == 0) {
                unsigned int hc = std::thread::hardware_concurrency();
                indexThreads = (hc == 0) ? 4u : static_cast<std::size_t>(hc);
            }
            if (indexThreads == 0) indexThreads = 1;

            std::cout << "Indexing directory: " << baseDir.generic_string() << "\n";
            IndexManager::IndexMetrics m;
            if (cfg.clearIndexOnStart) {
                indexManager.clearAll();
            }
            bool ok = indexManager.indexDirectoryIncremental(baseDir, m, cfg.removeMissing, indexThreads);
            if (!ok) {
                std::cerr << "WARNING: index directory not found: "
                          << baseDir.generic_string() << "\n";
            } else {
                std::cout << "Index built. "
                          << "scanned=" << m.scanned
                          << " added=" << m.added
                          << " reindexed=" << m.reindexed
                          << " skipped=" << m.skipped
                          << " removed=" << m.removed
                          << " failed=" << m.failed
                          << " timeMs=" << m.timeMs
                          << "\n";
            }
        }

        if (cfg.enableScheduler && cfg.schedulerInterval.count() > 0) {
            indexScheduler.start(
                [this, removeMissing = cfg.removeMissing]() {
                    IndexManager::IndexMetrics m;
                    bool ok = indexManager.indexDirectoryIncremental(baseDir, m, removeMissing);
                    if (ok) {
                        std::ostringstream oss;
                        oss << "Scheduler index: scanned=" << m.scanned
                            << " added=" << m.added
                            << " reindexed=" << m.reindexed
                            << " skipped=" << m.skipped
                            << " removed=" << m.removed
                            << " failed=" << m.failed
                            << " timeMs=" << m.timeMs;
                        logger.info(oss.str());
                    } else {
                        logger.warn("Scheduler index: directory not found / not a directory");
                    }
                },
                cfg.schedulerInterval,
                &logger,
                "index-scheduler"
            );
        }

        std::cout << "Server listening on " << cfg.ip << ":" << cfg.port << "\n";

        if (!threadPoolStarted) {
            std::size_t threads = cfg.threadCount;
            if (threads == 0) {
                unsigned int hc = std::thread::hardware_concurrency();
                threads = (hc == 0) ? 4u : static_cast<std::size_t>(hc);
            }
            threadPool.start(threads);
            threadPoolStarted = true;
        }

        return true;
    }

    std::filesystem::path resolvePathToBaseDir(const std::string& raw) const {
            std::filesystem::path p(raw);

        if (p.is_absolute()) {
            return p;
        }

        return baseDir / p;
    }

    void acceptLoop() {
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "Server not initialized\n";
            return;
        }

        while (!stopRequested.load()) {
            sockaddr_in clientAddr;
        #ifdef _WIN32
            int addrLen = sizeof(clientAddr);
        #else
            socklen_t addrLen = sizeof(clientAddr);
        #endif

            auto clientSocket = ::accept(listenSocket,
                                         reinterpret_cast<sockaddr*>(&clientAddr),
                                         &addrLen);
            if (clientSocket == INVALID_SOCKET) {
                if (stopRequested.load()) {
                    break;
                }
                continue;
            }

            std::string remote = addrToString(clientAddr);

            bool queued = threadPool.enqueue([this, clientSocket, remote]() {
                this->handleClient(clientSocket, remote);
            });

            if (!queued) {
                closeSocket(clientSocket);
            }
        }
    }

    void stop() {
        bool already = stopRequested.exchange(true);
        (void)already;
        indexScheduler.stop();

        if (listenSocket != INVALID_SOCKET) {
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
        }

        if (threadPoolStarted) {
            threadPool.stop();
            threadPoolStarted = false;
        }
    }

    IndexManager& getIndexManager() {
        return indexManager;
    }

private:
    static std::string addrToString(const sockaddr_in& addr) {
        char ipBuf[64] = {0};
        const char* ip = nullptr;
#ifdef _WIN32
        ip = inet_ntop(AF_INET, (void*)&addr.sin_addr, ipBuf, sizeof(ipBuf));
#else
        ip = inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
#endif
        std::ostringstream oss;
        oss << (ip ? ip : "unknown") << ":" << ntohs(addr.sin_port);
        return oss.str();
    }

#ifndef _WIN32
    static void posixSignalHandler(int) {
        if (instanceForSignals) {
            instanceForSignals->getLogger().warn("Signal received -> stopping server");
            instanceForSignals->requestStop();
        }
    }
#else
    static BOOL WINAPI consoleCtrlHandler(DWORD type) {
        if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) {
            if (instanceForSignals) {
                instanceForSignals->getLogger().warn("Console Ctrl event -> stopping server");
                instanceForSignals->requestStop();
            }
            return TRUE;
        }
        return FALSE;
    }
#endif


    void handleClient(
    #ifdef _WIN32
        SOCKET clientSocket
    #else
        int clientSocket
    #endif
        , const std::string& remote
    ) {
        std::string requestLine;
        bool ok = recvLine(clientSocket, requestLine);
        if (!ok) {
            closeSocket(clientSocket);
            return;
        }

        logger.info(std::string("Request from ") + remote + ": " + requestLine);

        std::string response = processRequest(requestLine);
        if (!response.empty()) {
            sendAll(clientSocket, response.c_str(), response.size());
        }

        closeSocket(clientSocket);
    }

    bool recvLine(
    #ifdef _WIN32
        SOCKET s,
    #else
        int s,
    #endif
        std::string& outLine
    ) {
        outLine.clear();

        const std::size_t MAX_LINE = 16 * 1024;

        char buf[512];
        while (outLine.size() < MAX_LINE) {
            int n = ::recv(s, buf, static_cast<int>(sizeof(buf)), 0);

            if (n == 0) {
                return !outLine.empty();
            }

            if (n < 0) {
                return false;
            }

            outLine.append(buf, buf + n);

            std::size_t pos = outLine.find('\n');
            if (pos != std::string::npos) {
                outLine.resize(pos);
                if (!outLine.empty() && outLine.back() == '\r') {
                    outLine.pop_back();
                }
                return true;
            }
        }

        return false;
    }

    bool sendAll(
    #ifdef _WIN32
        SOCKET s,
    #else
        int s,
    #endif
        const char* data,
        std::size_t len
    ) {
        std::size_t sentTotal = 0;
        while (sentTotal < len) {
            const char* ptr = data + sentTotal;
            std::size_t left = len - sentTotal;

            int chunk = 0;
#ifdef _WIN32
            chunk = ::send(s, ptr, static_cast<int>(left), 0);
#else
            chunk = static_cast<int>(::send(s, ptr, left, 0));
#endif

            if (chunk <= 0) {
                return false;
            }
            sentTotal += static_cast<std::size_t>(chunk);
        }
        return true;
    }

    std::string processRequest(const std::string& request) {
        Stopwatch totalSw;
        std::istringstream iss(request);
        std::string commandStr;
        iss >> commandStr;

        const auto cmdOpt = parseCommand(commandStr);
        if (!cmdOpt) {
            return ProtocolResponse::error("Unknown command", totalSw.elapsedMs());
        }

        auto docsTotal = [&]() { return indexManager.documentsCount(); };
        auto wordsTotal = [&]() { return indexManager.wordsCount(); };

        auto readRestAsPath = [&iss]() -> std::string {
            std::string rest;
            std::getline(iss, rest);
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
                rest.erase(rest.begin());
            }
            while (!rest.empty() && (rest.back() == '\r' || std::isspace(static_cast<unsigned char>(rest.back())))) {
                rest.pop_back();
            }
            if (rest.size() >= 2 && ((rest.front() == '"' && rest.back() == '"') || (rest.front() == '\'' && rest.back() == '\''))) {
                rest = rest.substr(1, rest.size() - 2);
            }
            return rest;
        };

               switch (*cmdOpt) {
            case CommandCode::INDEX_ALL: {
                IndexManager::IndexMetrics m;
                Stopwatch sw;
                bool ok = indexManager.indexDirectoryIncremental(baseDir, m, /*removeMissing*/true, indexThreads);
                if (!ok) {
                    return ProtocolResponse::error("Index directory not found", totalSw.elapsedMs());
                }
                std::ostringstream body;
                body << "SCANNED "   << m.scanned   << "\n"
                     << "ADDED "     << m.added     << "\n"
                     << "REINDEXED " << m.reindexed << "\n"
                     << "SKIPPED "   << m.skipped   << "\n"
                     << "REMOVED "   << m.removed   << "\n"
                     << "FAILED "    << m.failed    << "\n"
                     << "INDEX_MS "  << m.timeMs    << "\n";
                logger.info("INDEX_ALL done in " + std::to_string(sw.elapsedMs()) + "ms");
                return ProtocolResponse::ok(m.added + m.reindexed,
                                            totalSw.elapsedMs(),
                                            docsTotal(),
                                            wordsTotal(),
                                            body.str());
            }

            case CommandCode::ADD_FILE: {
                std::string raw = readRestAsPath();
                std::string path = resolvePathToBaseDir(raw).generic_string();
                if (path.empty()) return ProtocolResponse::error("Missing path for ADD_FILE", totalSw.elapsedMs());

                unsigned int id = 0;
                if (indexManager.hasFile(path, id)) {
                    return ProtocolResponse::error("File already indexed", totalSw.elapsedMs());
                }

                Stopwatch sw;
                bool ok = indexManager.addFile(path);
                if (!ok) return ProtocolResponse::error("Could not read file (not found / no access)", totalSw.elapsedMs());
                std::ostringstream body;
                body << "OP ADD_FILE\n"
                     << "OP_MS " << sw.elapsedMs() << "\n";
                return ProtocolResponse::ok(1, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
            }

            case CommandCode::REINDEX_FILE: {
                std::string raw = readRestAsPath();
                std::string path = resolvePathToBaseDir(raw).generic_string();

                if (path.empty()) return ProtocolResponse::error("Missing path for REINDEX_FILE", totalSw.elapsedMs());

                Stopwatch sw;
                bool ok = indexManager.reindexFile(path);
                if (!ok) return ProtocolResponse::error("Could not read file (not found / no access)", totalSw.elapsedMs());
                std::ostringstream body;
                body << "OP REINDEX_FILE\n"
                     << "OP_MS " << sw.elapsedMs() << "\n";
                return ProtocolResponse::ok(1, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
            }

              case CommandCode::HAS_FILE: {
                std::string raw = readRestAsPath();
                std::string path = resolvePathToBaseDir(raw).generic_string();
                if (path.empty()) return ProtocolResponse::error("Missing path for HAS_FILE", totalSw.elapsedMs());

                 unsigned int id = 0;
                bool found = indexManager.hasFile(path, id);
                if (!found) {
                    return ProtocolResponse::ok(0, totalSw.elapsedMs(), docsTotal(), wordsTotal(), "");
                }
                return ProtocolResponse::ok(1, totalSw.elapsedMs(), docsTotal(), wordsTotal(), "YES\n");
            }

            case CommandCode::REMOVE_FILE: {
                std::string raw = readRestAsPath();
                std::string path = resolvePathToBaseDir(raw).generic_string();

                if (path.empty()) return ProtocolResponse::error("Missing path for REMOVE_FILE", totalSw.elapsedMs());

                Stopwatch sw;
                bool ok = indexManager.removeFile(path);
                if (!ok) return ProtocolResponse::error("File not in index", totalSw.elapsedMs());
                std::ostringstream body;
                body << "OP REMOVE_FILE\n"
                     << "OP_MS " << sw.elapsedMs() << "\n";
                return ProtocolResponse::ok(1, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
            }

            case CommandCode::INDEX_DIR:
            case CommandCode::REINDEX_DIR:
            case CommandCode::REBUILD_INDEX: {
                std::string dir = readRestAsPath();
                if (dir.empty()) return ProtocolResponse::error("Missing dir path", totalSw.elapsedMs());

                if (*cmdOpt == CommandCode::REBUILD_INDEX) {
                    indexManager.clearAll();
                }

                Stopwatch sw;
                if (*cmdOpt == CommandCode::INDEX_DIR) {
                    std::size_t indexed = 0;
                    std::size_t failed  = 0;
                    bool ok = indexManager.indexDirectory(dir, indexed, failed, /*reindexExisting*/false);
                    if (!ok) return ProtocolResponse::error("Directory not found / not a directory", totalSw.elapsedMs());
                    std::ostringstream body;
                    body << "OP INDEX_DIR\n"
                         << "INDEXED " << indexed << "\n"
                         << "FAILED "  << failed  << "\n"
                         << "OP_MS "   << sw.elapsedMs() << "\n";
                    return ProtocolResponse::ok(indexed, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
                }

                if (*cmdOpt == CommandCode::REINDEX_DIR) {
                    std::size_t indexed = 0;
                    std::size_t failed  = 0;
                    bool ok = indexManager.indexDirectory(dir, indexed, failed, /*reindexExisting*/true);
                    if (!ok) return ProtocolResponse::error("Directory not found / not a directory", totalSw.elapsedMs());
                    std::ostringstream body;
                    body << "OP REINDEX_DIR\n"
                         << "INDEXED " << indexed << "\n"
                         << "FAILED "  << failed  << "\n"
                         << "OP_MS "   << sw.elapsedMs() << "\n";
                    return ProtocolResponse::ok(indexed, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
                }

                IndexManager::IndexMetrics m;
                bool ok = indexManager.indexDirectoryIncremental(dir, m, /*removeMissing*/true, indexThreads);
                if (!ok) return ProtocolResponse::error("Directory not found / not a directory", totalSw.elapsedMs());
 

                std::ostringstream body;
                body << "OP REBUILD_INDEX\n"
                     << "SCANNED "   << m.scanned   << "\n"
                     << "ADDED "     << m.added     << "\n"
                     << "REINDEXED " << m.reindexed << "\n"
                     << "SKIPPED "   << m.skipped   << "\n"
                     << "REMOVED "   << m.removed   << "\n"
                     << "FAILED "    << m.failed    << "\n"
                     << "INDEX_MS "  << m.timeMs    << "\n"
                     << "OP_MS "     << sw.elapsedMs() << "\n";
                return ProtocolResponse::ok(m.added + m.reindexed, totalSw.elapsedMs(), docsTotal(), wordsTotal(), body.str());
            }

            case CommandCode::SEARCH_ONE: {
                std::string word;
                iss >> word;
                if (word.empty()) {
                    return ProtocolResponse::error("Missing word for SEARCH_ONE", totalSw.elapsedMs());
                }

                std::vector<std::string> results;
                Stopwatch sw;
                bool found = indexManager.searchSingleWord(word, results);
                return ProtocolResponse::search(found, results, sw.elapsedMs(), totalSw.elapsedMs(), docsTotal(), wordsTotal());
            }

            case CommandCode::SEARCH_ALL:
            case CommandCode::SEARCH_ANY: {
                std::vector<std::string> words;
                std::string w;
                while (iss >> w) {
                    words.push_back(w);
                }
                if (words.empty()) {
                    return ProtocolResponse::error("No words provided", totalSw.elapsedMs());
                }

                std::vector<std::string> results;
                bool found = false;
                Stopwatch sw;
                if (*cmdOpt == CommandCode::SEARCH_ALL) {
                    found = indexManager.searchAllWords(words, results);
                } else {
                    found = indexManager.searchAnyWord(words, results);
                }
                return ProtocolResponse::search(found, results, sw.elapsedMs(), totalSw.elapsedMs(), docsTotal(), wordsTotal());
            }
        }

        return ProtocolResponse::error("Unknown command", totalSw.elapsedMs());
    }

    void closeSocket(
    #ifdef _WIN32
        SOCKET s
    #else
        int s
    #endif
    ) {
    #ifdef _WIN32
        ::closesocket(s);
    #else
        ::close(s);
    #endif
    }

private:
#ifdef _WIN32
    SOCKET listenSocket;
#else
    int listenSocket;
#endif

    std::atomic<bool> stopRequested;
    bool threadPoolStarted;
    ThreadPool threadPool;

    IndexManager indexManager;
    std::filesystem::path baseDir = std::filesystem::path("text_files") / "aclImdb";
    Logger logger;

    RepeatingTask indexScheduler;
    std::size_t indexThreads = 1;

    static inline Server* instanceForSignals = nullptr;
};

#endif
