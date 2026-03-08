#include "chatserver.hpp"
#include "chatservice.hpp"
#include "config.hpp"
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

    // 客户端入口的 worker 线程数做成配置项。
    // 为什么要这样做：前面的指标已经表明，oneChat 的主要等待时间落在“目标客户端连接所属 loop 的排队”上。
    // 因此这里需要能单独调节 client-facing I/O 线程，而不是把瓶颈继续归咎于 ClusterRouter。
    loadEnvFile();
    int ioThreads = getEnvIntOrDefault("CHAT_SERVER_IO_THREADS", 4);
    if (ioThreads <= 0)
    {
        ioThreads = 4;
    }
    server_.setThreadNum(ioThreads);
}

void ChatServer::start()
{
    server_.start();
}

// 处理用户连接的创建和断开
void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setContext(string());
        return;
    }

    ChatService::instance()->clientCloseException(conn);
    conn->shutdown();
}

// 处理用户的读写事件
void ChatServer::onMessage(const TcpConnectionPtr& conn,
               Buffer* buffer,
               Timestamp time)  // 接收到数据的时间信息
{
    if (conn->getContext().empty())
    {
        conn->setContext(string());
    }

    string* pending = boost::any_cast<string>(conn->getMutableContext());
    if (pending == nullptr)
    {
        LOG_ERROR << "client connection context type mismatch";
        conn->setContext(string());
        pending = boost::any_cast<string>(conn->getMutableContext());
    }

    pending->append(buffer->retrieveAllAsString());

    // 按 \0 分割处理完整消息；没有收完整的尾巴留在连接上下文里，等下一次字节到达。
    size_t start = 0;
    while (true)
    {
        size_t end = pending->find('\0', start);
        if (end == string::npos)
        {
            break;
        }

        if (end > start)
        {
            try
            {
                json js = json::parse(pending->begin() + start, pending->begin() + end);
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

    if (start > 0)
    {
        pending->erase(0, start);
    }
}

