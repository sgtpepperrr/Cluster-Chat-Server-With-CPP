#ifndef CLUSTERROUTER_H
#define CLUSTERROUTER_H

#include <memory>
#include <mutex>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/TcpClient.h>
#include "clustertypes.hpp"

class ClusterRouter
{
public:
    // 数据面发送器：
    // 当业务层已经知道目标用户属于哪个节点后，
    // 由该模块负责复用/建立到目标节点的 TCP 长连接并发送内部消息。
    explicit ClusterRouter(muduo::net::EventLoop* baseLoop);
    ~ClusterRouter();

    bool sendOneChat(const NodeMeta& nodeMeta,
                     const std::string& fromNode,
                     int toUser,
                     const std::string& payload,
                     const std::string& traceId);

    bool sendGroupChat(const NodeMeta& nodeMeta,
                       const std::string& fromNode,
                       const std::vector<int>& toUsers,
                       const std::string& payload,
                       const std::string& traceId);

    void prewarmNode(const NodeMeta& nodeMeta);
    void closeAll();

private:
    struct ShardChannel
    {
        ShardChannel(const std::string& id,
                     const NodeMeta& meta,
                     muduo::net::EventLoop* loop,
                     std::size_t limit,
                     std::size_t waterMark)
            : channelId(id)
            , nodeMeta(meta)
            , ioLoop(loop)
            , connecting(false)
            , overloaded(false)
            , closing(false)
            , pendingLimit(limit)
            , highWaterMark(waterMark)
        {
        }

        std::string channelId;
        NodeMeta nodeMeta;
        muduo::net::EventLoop* ioLoop;
        std::unique_ptr<muduo::net::TcpClient> client;
        muduo::net::TcpConnectionPtr connection;
        std::deque<std::string> pending;
        bool connecting;
        bool overloaded;
        bool closing;
        std::size_t pendingLimit;
        std::size_t highWaterMark;
        std::mutex mutex;
    };

    bool sendInternalMessage(const NodeMeta& nodeMeta,
                             std::size_t shardIndex,
                             const std::string& message);
    std::shared_ptr<ShardChannel> getOrCreateChannel(const NodeMeta& nodeMeta,
                                                     std::size_t shardIndex);
    void ensureClientInLoop(const std::shared_ptr<ShardChannel>& channel);
    bool enqueueOrSend(const std::shared_ptr<ShardChannel>& channel,
                       const std::string& message);
    void scheduleConnect(const std::shared_ptr<ShardChannel>& channel);
    void handleConnection(const std::shared_ptr<ShardChannel>& channel,
                          const muduo::net::TcpConnectionPtr& conn);
    void handleWriteComplete(const std::shared_ptr<ShardChannel>& channel,
                             const muduo::net::TcpConnectionPtr& conn);
    void handleHighWaterMark(const std::shared_ptr<ShardChannel>& channel,
                             const muduo::net::TcpConnectionPtr& conn,
                             std::size_t bytesToSend);
    void stopChannel(const std::shared_ptr<ShardChannel>& channel);
    bool sameEndpoint(const NodeMeta& lhs, const NodeMeta& rhs) const;
    std::size_t pickShard(int userId) const;
    std::string makeChannelKey(const std::string& nodeId, std::size_t shardIndex) const;

    muduo::net::EventLoop* baseLoop_;
    std::unique_ptr<muduo::net::EventLoopThreadPool> ioThreadPool_;
    std::vector<muduo::net::EventLoop*> ioLoops_;
    std::mutex connMapMutex_;
    std::mutex warmNodesMutex_;
    std::unordered_map<std::string, std::string> warmedNodeInstances_;
    std::unordered_map<std::string, std::shared_ptr<ShardChannel>> connMap_;
    std::size_t shardCount_;
    std::size_t connectingQueueLimit_;
    std::size_t highWaterMark_;
    int ioThreadCount_;
};

#endif /* CLUSTERROUTER_H */
