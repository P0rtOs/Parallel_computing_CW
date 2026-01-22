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
#include <cstring>
#include <algorithm>
#include <cctype>

#include "index_manager.h"
#include "data_structure/thread_pool.h"
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
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

    bool initServer(const std::string& ip, int port) {
        // Створити сокет
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
        addr.sin_port   = htons(static_cast<std::uint16_t>(port));
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            std::cerr << "inet_addr() failed for ip: " << ip << "\n";
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

        baseDir = std::filesystem::path("text_files") / "aclImdb";
        {
            std::cout << "Indexing directory: " << baseDir.generic_string() << "\n";
            bool ok = indexManager.indexDirectory(baseDir, true);
            if (!ok) {
                std::cerr << "WARNING: index directory not found: "
                          << baseDir.generic_string() << "\n";
            } else {
                std::cout << "Index built.\n";
            }
        }

        std::cout << "Server listening on " << ip << ":" << port << "\n";

        if (!threadPoolStarted) {
            unsigned int hc = std::thread::hardware_concurrency();
            if (hc == 0) {
                hc = 4;
            }
            threadPool.start(static_cast<std::size_t>(hc));
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
                std::cerr << "accept() failed\n";
                break;
            }

            bool queued = threadPool.enqueue([this, clientSocket]() {
                this->handleClient(clientSocket);
            });

            if (!queued) {
                closeSocket(clientSocket);
            }
        }
    }

    void stop() {
        stopRequested.store(true);

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

    void handleClient(
    #ifdef _WIN32
        SOCKET clientSocket
    #else
        int clientSocket
    #endif
    ) {
        std::string requestLine;
        bool ok = recvLine(clientSocket, requestLine);
        if (!ok) {
            closeSocket(clientSocket);
            return;
        }

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

        const std::size_t MAX_LINE = 16 * 1024; // 16 KB

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
            // на linux send() приймає size_t, але повертає ssize_t
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
        std::istringstream iss(request);
        std::string command;
        iss >> command;

        if (command == "INDEX_ALL") {
            bool ok = indexManager.indexDirectory(baseDir, true);
            if (!ok) {
                return "ERROR Index directory not found\n";
            }
            return "OK 1\nINDEXED\nEND\n";
        }

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

        auto okSimple = []() -> std::string {
            return "OK 1\nEND\n";
        };

        auto err = [](const std::string& msg) -> std::string {
            return "ERROR " + msg + "\nEND\n";
        };

        if (command == "ADD_FILE") {
            std::string raw = readRestAsPath();
            std::string path = resolvePathToBaseDir(raw).generic_string();
            if (path.empty()) return err("Missing path for ADD_FILE");

            unsigned int id = 0;
            if (indexManager.hasFile(path, id)) {
                return err("File already indexed");
            }

            bool ok = indexManager.addFile(path);
            return ok ? okSimple() : err("Could not read file (not found / no access)");
        }

        if (command == "REINDEX_FILE") {
            std::string raw = readRestAsPath();
            std::string path = resolvePathToBaseDir(raw).generic_string();

            if (path.empty()) return err("Missing path for REINDEX_FILE");

            bool ok = indexManager.reindexFile(path);
            return ok ? okSimple() : err("Could not read file (not found / no access)");
        }

        if (command == "HAS_FILE") {
            std::string raw = readRestAsPath();
            std::string path = resolvePathToBaseDir(raw).generic_string();
            if (path.empty()) return err("Missing path for HAS_FILE");

            unsigned int id = 0;
            bool found = indexManager.hasFile(path, id);
            if (!found) {
                return "OK 0\nEND\n";
            }
            return "OK 1\nYES\nEND\n";
        }

        if (command == "REMOVE_FILE") {
            std::string raw = readRestAsPath();
            std::string path = resolvePathToBaseDir(raw).generic_string();

            if (path.empty()) return err("Missing path for REMOVE_FILE");

            bool ok = indexManager.removeFile(path);
            return ok ? okSimple() : err("File not in index");
        }

        if (command == "INDEX_DIR" || command == "REINDEX_DIR" || command == "REBUILD_INDEX") {
            std::string dir = readRestAsPath();
            if (dir.empty()) return err("Missing dir path");

            if (command == "REBUILD_INDEX") {
                indexManager.clearAll();
            }

            std::size_t indexed = 0;
            std::size_t failed  = 0;
            bool reindexExisting = (command == "REINDEX_DIR");

            bool ok = indexManager.indexDirectory(dir, indexed, failed, reindexExisting);
            if (!ok) return err("Directory not found / not a directory");

            std::ostringstream oss;
            oss << "OK " << indexed << "\n";
            oss << "FAILED " << failed << "\n";
            oss << "END\n";
            return oss.str();
        }

        if (command == "SEARCH_ONE") {
            std::string word;
            iss >> word;
            if (word.empty()) {
                return "ERROR Missing word for SEARCH_ONE\n";
            }

            std::vector<std::string> results;
            bool found = indexManager.searchSingleWord(word, results);
            return formatSearchResponse(found, results);
        }

        if (command == "SEARCH_ALL" || command == "SEARCH_ANY") {
            std::vector<std::string> words;
            std::string w;
            while (iss >> w) {
                words.push_back(w);
            }
            if (words.empty()) {
                return "ERROR No words provided\n";
            }

            std::vector<std::string> results;
            bool found = false;
            if (command == "SEARCH_ALL") {
                found = indexManager.searchAllWords(words, results);
            } else {
                found = indexManager.searchAnyWord(words, results);
            }
            return formatSearchResponse(found, results);
        }

        return "ERROR Unknown command\n";
    }

    std::string formatSearchResponse(bool found,
                                     const std::vector<std::string>& results) {
        if (!found || results.empty()) {
            return "OK 0\nEND\n";
        }

        std::ostringstream oss;
        oss << "OK " << results.size() << "\n";
        for (const std::string& path : results) {
            oss << path << "\n";
        }
        oss << "END\n";
        return oss.str();
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
};

#endif
