#ifndef CLUSTERSERVER_H
#define CLUSTERSERVER_H

#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

using namespace muduo;
using namespace muduo::net;

class ClusterServer
{
public:
    // 内部收件口：只接收其他 ChatServer 节点发来的内部路由消息，
    // 不直接处理客户端登录/单聊/群聊协议。
    ClusterServer(EventLoop* loop,
                  const InetAddress& listenAddr,
                  const string& nameArg);

    void start();

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time);

    TcpServer server_;
    EventLoop* loop_;
};

#endif /* CLUSTERSERVER_H */
