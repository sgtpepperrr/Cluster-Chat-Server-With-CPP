#include "chatserver.hpp"
#include "chatservice.hpp"
#include "json.hpp"
#include <muduo/base/Logging.h>
#include <functional>
#include <string>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

ChatServer::ChatServer(EventLoop* loop,
               const InetAddress& listenAddr,  // IP + port
               const string& nameArg)          // 服务器的名字
    : server_(loop, listenAddr, nameArg)
    , loop_(loop)
{
    // 给服务器注册用户连接的创建和断开的回调
    server_.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 给服务器注册用户读写事件的回调
    server_.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置服务器端的线程数量
    server_.setThreadNum(4);
}

void ChatServer::start()
{
    server_.start();
}

// 处理用户连接的创建和断开
void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 处理用户的读写事件
void ChatServer::onMessage(const TcpConnectionPtr& conn,
               Buffer* buffer,
               Timestamp time)  // 接收到数据的时间信息
{
    string buf = buffer->retrieveAllAsString();

    // 按 \0 分割处理每条消息（处理 TCP 粘包）
    size_t start = 0;
    while (start < buf.size())
    {
        size_t end = buf.find('\0', start);
        if (end == string::npos)
        {
            end = buf.size();
        }

        if (end > start)
        {
            try
            {
                json js = json::parse(buf.begin() + start, buf.begin() + end);
                auto msgHandler = ChatService::instance()->getHandler(js["msgId"].get<int>());
                msgHandler(conn, js, time);
            }
            catch (const json::exception& e)
            {
                LOG_ERROR << "onMessage parse error: " << e.what();
            }
        }

        start = end + 1;
    }
}

