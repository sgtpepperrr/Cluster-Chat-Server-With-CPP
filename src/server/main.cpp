#include "chatserver.hpp"
#include "chatservice.hpp"
#include "clusterserver.hpp"
#include "config.hpp"
#include <iostream>
#include <signal.h>

using namespace std;

// Ctrl + C
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }

    char* ip = argv[1];
    uint16_t port = atoi(argv[2]);

    loadEnvFile();
    string interIp = getEnvOrDefault("CHAT_INTER_NODE_IP", ip);
    int interPort = getEnvIntOrDefault("CHAT_INTER_NODE_PORT", port + 100);

    signal(SIGINT, resetHandler);

    EventLoop loop;
    InetAddress addr(ip, port);
    InetAddress interAddr(interIp, static_cast<uint16_t>(interPort));
    ChatServer server(&loop, addr, "ChatServer");
    ClusterServer clusterServer(&loop, interAddr, "ClusterServer");

    ChatService::instance()->initNode(&loop, ip, port, interIp, static_cast<uint16_t>(interPort));

    server.start();
    clusterServer.start();
    loop.loop();

    return 0;
}
