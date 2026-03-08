#include "clusterserver.hpp"
#include "chatservice.hpp"
#include "config.hpp"
#include "json.hpp"

#include <muduo/base/Logging.h>
#include <functional>
#include <string>
#include <cstdint>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

namespace
{
// 收件端同时兼容两种内部协议：
// 1) oneChat 轻量帧 ONC1
// 2) 旧的 JSON\0（当前 groupChat 仍走这条路径）
const char kOneChatFrameMagic[] = {'O', 'N', 'C', '1'};
const size_t kOneChatFrameMagicBytes = sizeof(kOneChatFrameMagic);
const size_t kOneChatFrameHeaderBytes = 8;
const size_t kOneChatFrameBodyPrefixBytes = 4;

uint32_t readUint32(const char* data)
{
    return (static_cast<uint32_t>(static_cast<unsigned char>(data[0])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(data[3]));
}
}

ClusterServer::ClusterServer(EventLoop* loop,
                             const InetAddress& listenAddr,
                             const string& nameArg)
    : server_(loop, listenAddr, nameArg)
    , loop_(loop)
{
    server_.setConnectionCallback(std::bind(&ClusterServer::onConnection, this, _1));
    server_.setMessageCallback(std::bind(&ClusterServer::onMessage, this, _1, _2, _3));

    // 集群收件口线程数也做成配置项，但它的优先级低于客户端入口。
    // 这样可以把“客户端入口线程不足”和“内部收件线程不足”区分开来做压测。
    loadEnvFile();
    int ioThreads = getEnvIntOrDefault("CHAT_CLUSTER_IO_THREADS", 4);
    if (ioThreads <= 0)
    {
        ioThreads = 4;
    }
    server_.setThreadNum(ioThreads);
}

void ClusterServer::start()
{
    server_.start();
}

void ClusterServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
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

    pending->append(buffer->retrieveAllAsString());

    while (!pending->empty())
    {
        if (pending->front() == kOneChatFrameMagic[0])
        {
            if (pending->size() < kOneChatFrameMagicBytes)
            {
                break;
            }

            if (pending->compare(0, kOneChatFrameMagicBytes, kOneChatFrameMagic, kOneChatFrameMagicBytes) == 0)
            {
                if (pending->size() < kOneChatFrameMagicBytes + kOneChatFrameHeaderBytes)
                {
                    break;
                }

                uint32_t bodyLen = readUint32(pending->data() + kOneChatFrameMagicBytes);
                if (bodyLen < kOneChatFrameBodyPrefixBytes)
                {
                    LOG_ERROR << "cluster oneChat frame body too short: " << bodyLen;
                    pending->clear();
                    break;
                }

                size_t frameLen = kOneChatFrameMagicBytes + kOneChatFrameHeaderBytes + bodyLen - kOneChatFrameBodyPrefixBytes;
                if (pending->size() < frameLen)
                {
                    break;
                }

                int toUser = static_cast<int>(readUint32(pending->data() + kOneChatFrameMagicBytes + 4));
                string payload = pending->substr(kOneChatFrameMagicBytes + kOneChatFrameHeaderBytes,
                                                 bodyLen - kOneChatFrameBodyPrefixBytes);
                ChatService::instance()->handleClusterOneChatFrame(toUser, payload);
                pending->erase(0, frameLen);
                continue;
            }

            LOG_ERROR << "cluster unknown oneChat frame magic";
            pending->clear();
            break;
        }

        // 不是 ONC1 帧时，就按旧的 JSON\0 路径处理。
        size_t end = pending->find('\0');
        if (end == string::npos)
        {
            break;
        }

        if (end > 0)
        {
            try
            {
                json js = json::parse(pending->begin(), pending->begin() + end);
                ChatService::instance()->handleClusterMessage(js);
            }
            catch (const json::exception& e)
            {
                LOG_ERROR << "cluster onMessage parse error: " << e.what();
            }
        }

        pending->erase(0, end + 1);
    }
}
