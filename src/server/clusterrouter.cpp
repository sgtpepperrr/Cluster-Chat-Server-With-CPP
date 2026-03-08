#include "clusterrouter.hpp"
#include "config.hpp"
#include "json.hpp"
#include "public.hpp"

#include <muduo/base/Logging.h>
#include <functional>
#include <cstdint>

using namespace std;
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

namespace
{
// oneChat 走轻量内部帧：magic(4) + bodyLen(4) + toUser(4) + payload。
// 这样目标节点收到后不需要先解一层外部 JSON，再去取真正的聊天 JSON。
const char kOneChatFrameMagic[] = {'O', 'N', 'C', '1'};
const size_t kOneChatFrameHeaderBytes = 8;
const size_t kOneChatFrameBodyPrefixBytes = 4;

void appendUint32(string& out, uint32_t value)
{
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}
}

ClusterRouter::ClusterRouter(EventLoop* baseLoop)
    : baseLoop_(baseLoop)
    , shardCount_(4)
    , connectingQueueLimit_(256)
    , highWaterMark_(512 * 1024)
    , ioThreadCount_(1)
{
    loadEnvFile();

    int shards = getEnvIntOrDefault("CHAT_INTER_NODE_SHARDS", 4);
    if (shards > 0)
    {
        shardCount_ = static_cast<size_t>(shards);
    }

    int queueLimit = getEnvIntOrDefault(
        "CHAT_INTER_NODE_CONNECTING_QUEUE_LIMIT",
        getEnvIntOrDefault("CHAT_INTER_NODE_QUEUE_LIMIT", 256));
    if (queueLimit > 0)
    {
        connectingQueueLimit_ = static_cast<size_t>(queueLimit);
    }

    int highWaterMark = getEnvIntOrDefault("CHAT_INTER_NODE_HIGH_WATER_MARK", 512 * 1024);
    if (highWaterMark > 0)
    {
        highWaterMark_ = static_cast<size_t>(highWaterMark);
    }

    int ioThreads = getEnvIntOrDefault("CHAT_INTER_NODE_IO_THREADS", 0);
    if (ioThreads <= 0)
    {
        ioThreads = static_cast<int>(shardCount_);
        if (ioThreads > 4)
        {
            ioThreads = 4;
        }
        if (ioThreads <= 0)
        {
            ioThreads = 1;
        }
    }
    ioThreadCount_ = ioThreads;

    // 不同 nodeId#shard 会落到不同 I/O 线程上，减少单 loop 串行排队。
    // 但 TcpClient 的创建/连接/销毁仍然严格在所属 loop 线程内执行。
    ioThreadPool_.reset(new EventLoopThreadPool(baseLoop_, "ClusterRouterIO"));
    ioThreadPool_->setThreadNum(ioThreadCount_);
    ioThreadPool_->start();

    ioLoops_ = ioThreadPool_->getAllLoops();
    if (ioLoops_.empty())
    {
        ioLoops_.push_back(baseLoop_);
    }
}

ClusterRouter::~ClusterRouter()
{
    closeAll();
}

bool ClusterRouter::sendOneChat(const NodeMeta& nodeMeta,
                                const string& fromNode,
                                int toUser,
                                const string& payload,
                                const string& traceId)
{
    (void)fromNode;
    (void)traceId;

    if (!nodeMeta.valid())
    {
        return false;
    }

    // 只给 oneChat 做轻量帧优化；payload 仍然保持原始客户端消息 JSON。
    // 也就是说：我们减少的是‘节点间封装成本’，不是改客户端协议。
    string frame;
    frame.reserve(sizeof(kOneChatFrameMagic) + kOneChatFrameHeaderBytes + payload.size());
    frame.append(kOneChatFrameMagic, sizeof(kOneChatFrameMagic));
    appendUint32(frame, static_cast<uint32_t>(kOneChatFrameBodyPrefixBytes + payload.size()));
    appendUint32(frame, static_cast<uint32_t>(toUser));
    frame.append(payload);

    return sendInternalMessage(nodeMeta, pickShard(toUser), frame);
}

bool ClusterRouter::sendGroupChat(const NodeMeta& nodeMeta,
                                  const string& fromNode,
                                  const vector<int>& toUsers,
                                  const string& payload,
                                  const string& traceId)
{
    if (!nodeMeta.valid())
    {
        return false;
    }

    json js;
    js["msgId"] = NODE_ROUTE_GROUP_CHAT_MSG;
    js["fromNode"] = fromNode;
    js["toNode"] = nodeMeta.nodeId;
    js["toUsers"] = toUsers;
    js["payload"] = payload;
    js["traceId"] = traceId;

    size_t shardIndex = 0;
    if (!toUsers.empty())
    {
        shardIndex = pickShard(toUsers.front());
    }
    return sendInternalMessage(nodeMeta, shardIndex, js.dump() + '\0');
}

void ClusterRouter::prewarmNode(const NodeMeta& nodeMeta)
{
    if (!nodeMeta.valid())
    {
        return;
    }

    {
        lock_guard<mutex> lock(warmNodesMutex_);
        auto it = warmedNodeInstances_.find(nodeMeta.nodeId);
        if (it != warmedNodeInstances_.end() && it->second == nodeMeta.instanceId)
        {
            return;
        }

        warmedNodeInstances_[nodeMeta.nodeId] = nodeMeta.instanceId;
    }

    for (size_t shardIndex = 0; shardIndex < shardCount_; ++shardIndex)
    {
        shared_ptr<ShardChannel> channel = getOrCreateChannel(nodeMeta, shardIndex);
        bool shouldConnect = false;
        {
            lock_guard<mutex> lock(channel->mutex);
            if (!channel->closing &&
                !(channel->connection && channel->connection->connected()) &&
                !channel->connecting)
            {
                channel->connecting = true;
                shouldConnect = true;
            }
        }

        if (shouldConnect)
        {
            scheduleConnect(channel);
        }
    }
}

void ClusterRouter::closeAll()
{
    vector<shared_ptr<ShardChannel>> channels;
    {
        lock_guard<mutex> lock(connMapMutex_);
        for (auto& item : connMap_)
        {
            channels.push_back(item.second);
        }
        connMap_.clear();
    }

    for (const auto& channel : channels)
    {
        stopChannel(channel);
    }
}

bool ClusterRouter::sendInternalMessage(const NodeMeta& nodeMeta,
                                        size_t shardIndex,
                                        const string& message)
{
    shared_ptr<ShardChannel> channel = getOrCreateChannel(nodeMeta, shardIndex);
    return enqueueOrSend(channel, message);
}

shared_ptr<ClusterRouter::ShardChannel> ClusterRouter::getOrCreateChannel(const NodeMeta& nodeMeta,
                                                                          size_t shardIndex)
{
    const string channelKey = makeChannelKey(nodeMeta.nodeId, shardIndex);
    shared_ptr<ShardChannel> staleChannel;

    {
        lock_guard<mutex> lock(connMapMutex_);
        auto it = connMap_.find(channelKey);
        if (it != connMap_.end())
        {
            if (sameEndpoint(it->second->nodeMeta, nodeMeta))
            {
                return it->second;
            }

            staleChannel = it->second;
            connMap_.erase(it);
        }
    }

    if (staleChannel)
    {
        stopChannel(staleChannel);
    }

    EventLoop* ioLoop = nullptr;
    if (!ioLoops_.empty())
    {
        size_t loopIndex = hash<string>()(channelKey) % ioLoops_.size();
        ioLoop = ioLoops_[loopIndex];
    }
    if (ioLoop == nullptr)
    {
        ioLoop = baseLoop_;
    }

    shared_ptr<ShardChannel> channel(new ShardChannel(channelKey,
                                                      nodeMeta,
                                                      ioLoop,
                                                      connectingQueueLimit_,
                                                      highWaterMark_));

    lock_guard<mutex> lock(connMapMutex_);
    auto it = connMap_.find(channelKey);
    if (it != connMap_.end())
    {
        stopChannel(channel);
        return it->second;
    }

    connMap_[channelKey] = channel;
    return channel;
}

void ClusterRouter::ensureClientInLoop(const shared_ptr<ShardChannel>& channel)
{
    channel->ioLoop->assertInLoopThread();

    if (channel->closing || channel->client)
    {
        return;
    }

    InetAddress serverAddr(channel->nodeMeta.interIp, channel->nodeMeta.interPort);
    channel->client.reset(new TcpClient(channel->ioLoop, serverAddr, channel->channelId));
    channel->client->enableRetry();
    channel->client->setConnectionCallback(
        std::bind(&ClusterRouter::handleConnection, this, channel, std::placeholders::_1));
    channel->client->setWriteCompleteCallback(
        std::bind(&ClusterRouter::handleWriteComplete, this, channel, std::placeholders::_1));
}

bool ClusterRouter::enqueueOrSend(const shared_ptr<ShardChannel>& channel,
                                  const string& message)
{
    bool shouldConnect = false;
    bool shouldDrain = false;
    TcpConnectionPtr connection;

    {
        lock_guard<mutex> lock(channel->mutex);
        if (channel->closing)
        {
            return false;
        }

        bool connected = channel->connection && channel->connection->connected();

        // 热路径只在“已连接、未背压、没有历史积压”时直接 send。
        // 一旦连接正在背压，或者已经有排队消息，就统一先进 pending，避免：
        // 1) 临时 high-water 时直接丢消息
        // 2) 新消息绕过旧消息，破坏同 shard 内发送顺序
        if (connected && !channel->overloaded && channel->pending.empty())
        {
            connection = channel->connection;
        }
        else
        {
            if (channel->pending.size() >= channel->pendingLimit)
            {
                LOG_WARN << "cluster channel pending queue full channel=" << channel->channelId
                         << " size=" << channel->pending.size();
                return false;
            }

            channel->pending.push_back(message);
            if (!connected && !channel->connecting)
            {
                channel->connecting = true;
                shouldConnect = true;
            }
            else if (connected && !channel->overloaded)
            {
                shouldDrain = true;
            }
        }
    }

    if (connection)
    {
        connection->send(message);
        return true;
    }

    if (shouldConnect)
    {
        scheduleConnect(channel);
    }
    else if (shouldDrain)
    {
        channel->ioLoop->queueInLoop([this, channel]() {
            drainPendingInLoop(channel);
        });
    }

    return true;
}

void ClusterRouter::scheduleConnect(const shared_ptr<ShardChannel>& channel)
{
    channel->ioLoop->runInLoop([this, channel]() {
        {
            lock_guard<mutex> lock(channel->mutex);
            if (channel->closing || (channel->connection && channel->connection->connected()))
            {
                channel->connecting = false;
                return;
            }
        }

        ensureClientInLoop(channel);
        if (!channel->client)
        {
            lock_guard<mutex> lock(channel->mutex);
            channel->connecting = false;
            return;
        }

        channel->client->connect();
    });
}

void ClusterRouter::drainPendingInLoop(const shared_ptr<ShardChannel>& channel)
{
    channel->ioLoop->assertInLoopThread();

    while (true)
    {
        TcpConnectionPtr connection;
        string message;

        {
            lock_guard<mutex> lock(channel->mutex);
            if (channel->closing || channel->overloaded)
            {
                return;
            }

            if (!(channel->connection && channel->connection->connected()))
            {
                return;
            }

            if (channel->pending.empty())
            {
                return;
            }

            connection = channel->connection;
            message.swap(channel->pending.front());
            channel->pending.pop_front();
        }

        connection->send(message);
    }
}

void ClusterRouter::handleConnection(const shared_ptr<ShardChannel>& channel,
                                     const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setTcpNoDelay(true);
        conn->setHighWaterMarkCallback(
            std::bind(&ClusterRouter::handleHighWaterMark,
                      this,
                      channel,
                      std::placeholders::_1,
                      std::placeholders::_2),
            channel->highWaterMark);

        {
            lock_guard<mutex> lock(channel->mutex);
            channel->connection = conn;
            channel->connecting = false;
            channel->overloaded = false;
        }

        // 建连成功后，继续发送“建连期间”和“背压期间”暂存在 pending 里的消息。
        drainPendingInLoop(channel);
        return;
    }

    {
        lock_guard<mutex> lock(channel->mutex);
        channel->connection.reset();
        channel->overloaded = false;
        channel->connecting = !channel->closing;
    }
}

void ClusterRouter::handleWriteComplete(const shared_ptr<ShardChannel>& channel,
                                        const TcpConnectionPtr& conn)
{
    (void)conn;

    {
        lock_guard<mutex> lock(channel->mutex);
        channel->overloaded = false;
    }

    // 输出缓冲回落后，继续把背压期间积压的小队列往前推。
    drainPendingInLoop(channel);
}

void ClusterRouter::handleHighWaterMark(const shared_ptr<ShardChannel>& channel,
                                        const TcpConnectionPtr& conn,
                                        size_t bytesToSend)
{
    (void)conn;
    {
        lock_guard<mutex> lock(channel->mutex);
        channel->overloaded = true;
    }

    LOG_WARN << "cluster channel high water mark channel=" << channel->channelId
             << " bytes=" << bytesToSend;
}

void ClusterRouter::stopChannel(const shared_ptr<ShardChannel>& channel)
{
    TcpConnectionPtr connection;
    {
        lock_guard<mutex> lock(channel->mutex);
        channel->closing = true;
        channel->connecting = false;
        channel->pending.clear();
        connection = channel->connection;
        channel->connection.reset();
    }

    channel->ioLoop->queueInLoop([channel, connection]() {
        if (connection)
        {
            connection->forceClose();
        }

        if (channel->client)
        {
            channel->client->disconnect();
            channel->client->stop();
            channel->client.reset();
        }
    });
}

bool ClusterRouter::sameEndpoint(const NodeMeta& lhs, const NodeMeta& rhs) const
{
    return lhs.interIp == rhs.interIp && lhs.interPort == rhs.interPort;
}

size_t ClusterRouter::pickShard(int userId) const
{
    if (shardCount_ == 0)
    {
        return 0;
    }

    return static_cast<size_t>(userId >= 0 ? userId : -userId) % shardCount_;
}

string ClusterRouter::makeChannelKey(const string& nodeId, size_t shardIndex) const
{
    return nodeId + "#" + to_string(shardIndex);
}
