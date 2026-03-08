#include "clusterserver.hpp"
#include "chatservice.hpp"
#include "json.hpp"

#include <muduo/base/Logging.h>
#include <functional>
#include <string>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

ClusterServer::ClusterServer(EventLoop* loop,
                             const InetAddress& listenAddr,
                             const string& nameArg)
    : server_(loop, listenAddr, nameArg)
    , loop_(loop)
{
    server_.setConnectionCallback(std::bind(&ClusterServer::onConnection, this, _1));
    server_.setMessageCallback(std::bind(&ClusterServer::onMessage, this, _1, _2, _3));
    server_.setThreadNum(4);
}

void ClusterServer::start()
{
    server_.start();
}

void ClusterServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        // TCP 是字节流，不保证一次 onMessage 就是一条完整 JSON。
        // 因此每条内部连接都要挂一个“残留字节缓冲区”，把半包攒起来。
        conn->setContext(string());
        return;
    }

    conn->shutdown();
}

void ClusterServer::onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time)
{
    (void)time;

    if (conn->getContext().empty())
    {
        conn->setContext(string());
    }

    string* pending = boost::any_cast<string>(conn->getMutableContext());
    if (pending == nullptr)
    {
        LOG_ERROR << "cluster connection context type mismatch";
        conn->setContext(string());
        pending = boost::any_cast<string>(conn->getMutableContext());
    }

    // 内部协议仍旧使用 '\0' 作为分隔符，但现在按“流式字节缓冲”处理：
    // 先把本次读到的字节追加到连接级缓存里，再只解析完整帧，把半包留到下次。
    pending->append(buffer->retrieveAllAsString());

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
                ChatService::instance()->handleClusterMessage(js);
            }
            catch (const json::exception& e)
            {
                LOG_ERROR << "cluster onMessage parse error: " << e.what();
            }
        }

        start = end + 1;
    }

    if (start > 0)
    {
        pending->erase(0, start);
    }
}
