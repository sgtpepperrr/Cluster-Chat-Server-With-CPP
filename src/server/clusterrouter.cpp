#include "clusterrouter.hpp"
#include "config.hpp"
#include "json.hpp"
#include "public.hpp"

#include <muduo/base/Logging.h>
#include <functional>

using namespace std;
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

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

    // 现在恢复为多 inter-node loops：
    // 不同 nodeId#shard 会落到不同 I/O 线程上，减少单 loop 串行排队。
    // 但 TcpClient 的创建/连接/销毁仍然严格在所属 loop 线程内执行，
    // 保留前一轮修好的线程归属边界。
    ioThreadPool_.reset(new EventLoopThreadPool(baseLoop_, "ClusterRouterIO"));
    ioThreadPool_->setThreadNum(ioThreadCount_);
    ioThreadPool_->start();

    // 注意：EventLoopThreadPool 的取 loop 接口属于 Muduo 内部线程模型的一部分，
    // 不能在任意业务线程里随便调用，否则可能再次碰到 baseLoop 的线程约束。
    // 因此这里只在构造阶段（主线程）把所有 inter-node loops 取出来，后面热路径只做纯数组寻址。
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
    if (!nodeMeta.valid())
    {
        return false;
    }

    json js;
    js["msgId"] = NODE_ROUTE_ONE_CHAT_MSG;
    js["fromNode"] = fromNode;
    js["toNode"] = nodeMeta.nodeId;
    js["toUser"] = toUser;
    js["payload"] = payload;
    js["traceId"] = traceId;
    return sendInternalMessage(nodeMeta, pickShard(toUser), js.dump() + '\0');
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

    // 预热的目标不是发送业务消息，而是把“首次跨节点消息的建连成本”提前支付掉。
    // 关键点是：每个远端节点实例只预热一次，不能在 oneChat 热路径里反复扫所有 shard。
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

    // TcpClient 绑定哪个 EventLoop，就必须在哪个 EventLoop 线程里创建。
    // 上一版在 ChatServer 的 worker 线程里直接 new TcpClient，
    // 会触发 Muduo 的 abortNotInLoopThread。
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
    TcpConnectionPtr connection;
    bool shouldConnect = false;

    // 热路径优先做两件事：
    // 1) 已连上时直接交给 Muduo 连接发送
    // 2) 未连上时只做一个很小的瞬时缓冲，然后触发异步 connect
    // 这样业务线程不会再像旧实现那样，被 worker + 阻塞 write 串住。

    {
        lock_guard<mutex> lock(channel->mutex);
        if (channel->closing || channel->overloaded)
        {
            return false;
        }

        if (channel->connection && channel->connection->connected())
        {
            connection = channel->connection;
        }
        else
        {
            if (channel->pending.size() >= channel->pendingLimit)
            {
                return false;
            }

            channel->pending.push_back(message);
            if (!channel->connecting)
            {
                channel->connecting = true;
                shouldConnect = true;
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

        // 连接建立成功后，把“建连期间短暂攒下来的消息”一次性交给 Muduo。
        // 注意这里仍然不是手写 worker 队列，而只是 connect-gap 的瞬时缓冲。
        deque<string> pending;
        {
            lock_guard<mutex> lock(channel->mutex);
            channel->connection = conn;
            channel->connecting = false;
            channel->overloaded = false;
            pending.swap(channel->pending);
        }

        while (!pending.empty())
        {
            conn->send(pending.front());
            pending.pop_front();
        }

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
    lock_guard<mutex> lock(channel->mutex);
    channel->overloaded = false;
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
        // closeAll/节点下线时直接清空瞬时缓冲，避免旧连接上的残留消息继续外发。
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
