#ifndef SIMPLE_SERVER_H
#define SIMPLE_SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <sstream>

#include "IndexManager.h"
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
        : listenSocket(INVALID_SOCKET)
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
        addr.sin_port   = htons(static_cast<uint16_t>(port));
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

        std::cout << "Server listening on " << ip << ":" << port << "\n";
        return true;
    }

    void acceptLoop() {
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "Server not initialized\n";
            return;
        }

        while (true) {
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
                std::cerr << "accept() failed\n";
                break;
            }

            std::thread t(&Server::handleClient, this, clientSocket);
            t.detach();
        }
    }

    void stop() {
        if (listenSocket != INVALID_SOCKET) {
            closeSocket(listenSocket);
            listenSocket = INVALID_SOCKET;
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
        char buffer[4096];
        int received = ::recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closeSocket(clientSocket);
            return;
        }
        buffer[received] = '\0';

        std::string request(buffer);
        std::string response = processRequest(request);

        if (!response.empty()) {
            ::send(clientSocket, response.c_str(),
                   static_cast<int>(response.size()), 0);
        }

        closeSocket(clientSocket);
    }

    std::string processRequest(const std::string& request) {
        std::istringstream iss(request);
        std::string command;
        iss >> command;

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

    IndexManager indexManager;
};

#endif
