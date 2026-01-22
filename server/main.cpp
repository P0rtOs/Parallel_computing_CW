#include <iostream>
#include "server.h"

int main() {
    if (!Server::initSockets()) {
        return 1;
    }

    Server server;
    if (!server.initServer("127.0.0.1", 8080)) {
        Server::cleanupSockets();
        return 1;
    }

    server.acceptLoop();

    server.stop();
    Server::cleanupSockets();
    return 0;
}