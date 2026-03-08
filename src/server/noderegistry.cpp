#include "noderegistry.hpp"
#include "redis.hpp"

#include <chrono>

using namespace std;

NodeRegistry::NodeRegistry(Redis* redis)
    : redis_(redis)
    , heartbeatIntervalMs_(3000)
    , heartbeatTimeoutMs_(10000)
    , routeCacheTtlMs_(1000)
    , running_(false)
{
}

NodeRegistry::~NodeRegistry()
{
    stop();
}

bool NodeRegistry::init(const NodeMeta& selfNode, int heartbeatIntervalMs, int heartbeatTimeoutMs)
{
    selfNode_ = selfNode;
    heartbeatIntervalMs_ = heartbeatIntervalMs;
    heartbeatTimeoutMs_ = heartbeatTimeoutMs;
    if (heartbeatIntervalMs_ <= 0)
    {
        routeCacheTtlMs_ = 1000;
    }
    else if (heartbeatIntervalMs_ > 1000)
    {
        routeCacheTtlMs_ = 1000;
    }
    else
    {
        routeCacheTtlMs_ = heartbeatIntervalMs_;
    }

    cacheNode(selfNode_);

    if (!redis_->registerNode(selfNode_, heartbeatTimeoutMs_ / 1000 + 2))
    {
        return false;
    }

    running_ = true;
    heartbeatThread_ = thread(&NodeRegistry::heartbeatLoop, this);
    return true;
}

void NodeRegistry::stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false))
    {
        return;
    }

    if (heartbeatThread_.joinable())
    {
        heartbeatThread_.join();
    }
}

bool NodeRegistry::bindUser(int userId)
{
    if (!redis_->bindUserNode(userId, selfNode_.nodeId, selfNode_.instanceId))
    {
        return false;
    }

    cacheUserNode(userId, selfNode_.nodeId, selfNode_.instanceId);
    return true;
}

bool NodeRegistry::unbindUser(int userId)
{
    eraseCachedUserNode(userId);
    return redis_->unbindUserNode(userId, selfNode_.nodeId);
}

bool NodeRegistry::queryUserNode(int userId, string& nodeId)
{
    // 这里不再只校验“nodeId 是否存在”，还要校验“session 里记录的 instanceId 是否等于当前活节点实例”。
    // 原因：同一个 nodeId 崩溃重启后，旧 session 可能仍然残留在 Redis 中；
    // 如果只看 nodeId，会把“新节点”误认成“旧会话仍有效”。
    string instanceId;
    if (getCachedUserNode(userId, nodeId, instanceId))
    {
        NodeMeta nodeMeta;
        if (queryNode(nodeId, nodeMeta) && !instanceId.empty() && nodeMeta.instanceId == instanceId)
        {
            return true;
        }

        redis_->unbindUserNode(userId, nodeId);
        eraseCachedUserNode(userId);
        nodeId.clear();
        return false;
    }

    if (!redis_->getUserSession(userId, nodeId, instanceId))
    {
        return false;
    }

    cacheUserNode(userId, nodeId, instanceId);

    NodeMeta nodeMeta;
    if (!queryNode(nodeId, nodeMeta) || instanceId.empty() || nodeMeta.instanceId != instanceId)
    {
        redis_->unbindUserNode(userId, nodeId);
        eraseCachedUserNode(userId);
        nodeId.clear();
        return false;
    }

    return true;
}

bool NodeRegistry::queryNode(const string& nodeId, NodeMeta& nodeMeta)
{
    if (getCachedNode(nodeId, nodeMeta))
    {
        return true;
    }

    if (!redis_->getNode(nodeId, nodeMeta))
    {
        eraseCachedNode(nodeId);
        return false;
    }

    cacheNode(nodeMeta);
    return true;
}

bool NodeRegistry::resolveUserNode(int userId, string& nodeId, NodeMeta& nodeMeta)
{
    string instanceId;
    if (getCachedUserNode(userId, nodeId, instanceId) && getCachedNode(nodeId, nodeMeta) &&
        !instanceId.empty() && nodeMeta.instanceId == instanceId)
    {
        return true;
    }

    if (!queryUserNode(userId, nodeId))
    {
        return false;
    }

    return queryNode(nodeId, nodeMeta);
}

vector<NodeMeta> NodeRegistry::listNodes()
{
    vector<NodeMeta> nodes;
    vector<string> nodeIds = redis_->getNodeIds();
    nodes.reserve(nodeIds.size());

    for (const string& nodeId : nodeIds)
    {
        NodeMeta nodeMeta;
        if (queryNode(nodeId, nodeMeta))
        {
            nodes.push_back(nodeMeta);
        }
        else
        {
            eraseCachedNode(nodeId);
        }
    }

    return nodes;
}

void NodeRegistry::cleanupSelf()
{
    vector<int> users = redis_->getNodeUsers(selfNode_.nodeId);
    for (int userId : users)
    {
        redis_->unbindUserNode(userId, selfNode_.nodeId);
    }

    redis_->unregisterNode(selfNode_.nodeId);

    lock_guard<mutex> lock(cacheMutex_);
    userRouteCache_.clear();
    nodeMetaCache_.clear();
}

const NodeMeta& NodeRegistry::selfNode() const
{
    return selfNode_;
}

const string& NodeRegistry::selfNodeId() const
{
    return selfNode_.nodeId;
}

bool NodeRegistry::isSelfNode(const string& nodeId) const
{
    return selfNode_.nodeId == nodeId;
}

void NodeRegistry::heartbeatLoop()
{
    while (running_)
    {
        this_thread::sleep_for(chrono::milliseconds(heartbeatIntervalMs_));

        if (!running_)
        {
            break;
        }

        // 周期性续租节点元信息，让其他节点在查路由时知道“这个节点还活着”。
        selfNode_.heartbeatMs = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
        cacheNode(selfNode_);
        redis_->registerNode(selfNode_, heartbeatTimeoutMs_ / 1000 + 2);
    }
}

long long NodeRegistry::nowMs() const
{
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

bool NodeRegistry::getCachedUserNode(int userId, string& nodeId, string& instanceId)
{
    lock_guard<mutex> lock(cacheMutex_);
    auto it = userRouteCache_.find(userId);
    if (it == userRouteCache_.end())
    {
        return false;
    }

    if (it->second.expireAtMs < nowMs())
    {
        userRouteCache_.erase(it);
        return false;
    }

    nodeId = it->second.nodeId;
    instanceId = it->second.instanceId;
    return true;
}

void NodeRegistry::cacheUserNode(int userId, const string& nodeId, const string& instanceId)
{
    lock_guard<mutex> lock(cacheMutex_);
    userRouteCache_[userId] = UserRouteCacheEntry{nodeId, instanceId, nowMs() + routeCacheTtlMs_};
}

void NodeRegistry::eraseCachedUserNode(int userId)
{
    lock_guard<mutex> lock(cacheMutex_);
    userRouteCache_.erase(userId);
}

bool NodeRegistry::getCachedNode(const string& nodeId, NodeMeta& nodeMeta)
{
    lock_guard<mutex> lock(cacheMutex_);
    auto it = nodeMetaCache_.find(nodeId);
    if (it == nodeMetaCache_.end())
    {
        return false;
    }

    if (it->second.expireAtMs < nowMs())
    {
        nodeMetaCache_.erase(it);
        return false;
    }

    nodeMeta = it->second.nodeMeta;
    return true;
}

void NodeRegistry::cacheNode(const NodeMeta& nodeMeta)
{
    lock_guard<mutex> lock(cacheMutex_);
    nodeMetaCache_[nodeMeta.nodeId] = NodeMetaCacheEntry{nodeMeta, nowMs() + routeCacheTtlMs_};
}

void NodeRegistry::eraseCachedNode(const string& nodeId)
{
    lock_guard<mutex> lock(cacheMutex_);
    nodeMetaCache_.erase(nodeId);
}
