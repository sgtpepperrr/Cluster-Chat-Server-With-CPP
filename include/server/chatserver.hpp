#ifndef CHATSERVER_H
#define CHATSERVER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer
{
public:
    ChatServer(EventLoop* loop,
               const InetAddress& listenAddr,  // IP + port
               const string& nameArg);          // 服务器的名字

    void start();

private:
    // 处理用户连接的创建和断开
    void onConnection(const TcpConnectionPtr& conn);

    // 处理用户的读写事件
    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buffer,
                   Timestamp time);  // 接收到数据的时间信息

    TcpServer server_;
    EventLoop* loop_;
};

#endif /* CHATSERVER_H */
