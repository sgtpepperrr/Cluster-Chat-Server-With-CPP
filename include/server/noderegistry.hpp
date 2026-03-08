#ifndef NODEREGISTRY_H
#define NODEREGISTRY_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "clustertypes.hpp"

class Redis;

class NodeRegistry
{
public:
    // 控制面入口：
    // 1) 节点启动时注册自己并定期刷新心跳
    // 2) 维护 userId -> nodeId 的会话归属
    // 3) 查询用户归属时顺带校验目标节点是否仍然存活
    explicit NodeRegistry(Redis* redis);
    ~NodeRegistry();

    bool init(const NodeMeta& selfNode, int heartbeatIntervalMs, int heartbeatTimeoutMs);
    void stop();

    bool bindUser(int userId);
    bool unbindUser(int userId);
    bool queryUserNode(int userId, std::string& nodeId);
    bool queryNode(const std::string& nodeId, NodeMeta& nodeMeta);
    bool resolveUserNode(int userId, std::string& nodeId, NodeMeta& nodeMeta);
    std::vector<NodeMeta> listNodes();
    void cleanupSelf();

    const NodeMeta& selfNode() const;
    const std::string& selfNodeId() const;
    bool isSelfNode(const std::string& nodeId) const;

private:
    struct UserRouteCacheEntry
    {
        std::string nodeId;
        std::string instanceId;
        long long expireAtMs;
    };

    struct NodeMetaCacheEntry
    {
        NodeMeta nodeMeta;
        long long expireAtMs;
    };

    long long nowMs() const;
    bool getCachedUserNode(int userId, std::string& nodeId, std::string& instanceId);
    void cacheUserNode(int userId, const std::string& nodeId, const std::string& instanceId);
    void eraseCachedUserNode(int userId);
    bool getCachedNode(const std::string& nodeId, NodeMeta& nodeMeta);
    void cacheNode(const NodeMeta& nodeMeta);
    void eraseCachedNode(const std::string& nodeId);
    void heartbeatLoop();

    Redis* redis_;
    NodeMeta selfNode_;
    int heartbeatIntervalMs_;
    int heartbeatTimeoutMs_;
    int routeCacheTtlMs_;
    std::atomic<bool> running_;
    std::thread heartbeatThread_;

    std::mutex cacheMutex_;
    std::unordered_map<int, UserRouteCacheEntry> userRouteCache_;
    std::unordered_map<std::string, NodeMetaCacheEntry> nodeMetaCache_;
};

#endif /* NODEREGISTRY_H */
