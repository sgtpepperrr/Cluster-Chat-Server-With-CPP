#include "chatservice.hpp"
#include "clusterrouter.hpp"
#include "config.hpp"
#include "noderegistry.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <chrono>
#include <sstream>
#include <vector>

using namespace muduo;
using namespace std;

namespace
{
long long nowSteadyNs()
{
    return chrono::duration_cast<chrono::nanoseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

void updatePeak(atomic<long long>& peak, long long value)
{
    long long current = peak.load();
    while (current < value && !peak.compare_exchange_weak(current, value))
    {
    }
}
}

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

ChatService::ChatService()
    : connBucketCount_(64)
    , localDeliveryQueueLimit_(8192)
    , localDeliveryCount_(0)
    , remoteRouteCount_(0)
    , remoteRouteFailCount_(0)
    , offlineFallbackCount_(0)
    , staleSessionCount_(0)
    , oneChatSourceCount_(0)
    , oneChatResolveCount_(0)
    , oneChatResolveNs_(0)
    , oneChatRemoteSendCount_(0)
    , oneChatRemoteSendNs_(0)
    , oneChatRemoteHandleCount_(0)
    , oneChatRemoteHandleNs_(0)
    , localLookupCount_(0)
    , localLookupNs_(0)
    , localLookupMissCount_(0)
    , localDispatchEnqueueCount_(0)
    , localDispatchEnqueueNs_(0)
    , localDispatchQueuedCount_(0)
    , localDispatchInlineCount_(0)
    , localDispatchDrainMsgCount_(0)
    , localDispatchSendNs_(0)
    , localDispatchQueueWaitNs_(0)
    , localDispatchQueueWaitMaxNs_(0)
    , localDispatchPeakQueueDepth_(0)
{
    loadEnvFile();
    int localQueueLimit = getEnvIntOrDefault("CHAT_LOCAL_DELIVERY_QUEUE_LIMIT", 8192);
    if (localQueueLimit > 0)
    {
        localDeliveryQueueLimit_ = static_cast<size_t>(localQueueLimit);
    }

    for (size_t i = 0; i < connBucketCount_; ++i)
    {
        connBuckets_.push_back(unique_ptr<ConnectionBucket>(new ConnectionBucket()));
    }

    msgHandlerMap_.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    msgHandlerMap_.insert({LOGOUT_MSG, std::bind(&ChatService::logout, this, _1, _2, _3)});
    msgHandlerMap_.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    msgHandlerMap_.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    msgHandlerMap_.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    if (redis_.connect())
    {
        redis_.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }

}

void ChatService::initNode(EventLoop* baseLoop,
                           const string& clientIp,
                           uint16_t clientPort,
                           const string& interIp,
                           uint16_t interPort)
{
    selfNode_.clientIp = clientIp;
    selfNode_.clientPort = clientPort;
    selfNode_.interIp = interIp;
    selfNode_.interPort = interPort;
    selfNode_.nodeId = interIp + ":" + to_string(interPort);
    selfNode_.heartbeatMs = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    selfNode_.instanceId = selfNode_.nodeId + "#" + to_string(selfNode_.heartbeatMs);

    nodeRegistry_.reset(new NodeRegistry(&redis_));
    clusterRouter_.reset(new ClusterRouter(baseLoop));

    loadEnvFile();
    int heartbeatIntervalMs = getEnvIntOrDefault("CHAT_HEARTBEAT_INTERVAL_MS", 3000);
    int heartbeatTimeoutMs = getEnvIntOrDefault("CHAT_HEARTBEAT_TIMEOUT_MS", 10000);
    if (!nodeRegistry_->init(selfNode_, heartbeatIntervalMs, heartbeatTimeoutMs))
    {
        LOG_ERROR << "failed to initialize node registry for " << selfNode_.nodeId;
        return;
    }

    // 启动时把当前已知的远端节点先预热一遍。
    // 这样首轮 oneChat 压测时，不会所有跨节点消息都一起去触发 connect。
    vector<NodeMeta> nodes = nodeRegistry_->listNodes();
    for (const NodeMeta& nodeMeta : nodes)
    {
        if (!nodeRegistry_->isSelfNode(nodeMeta.nodeId) && clusterRouter_)
        {
            clusterRouter_->prewarmNode(nodeMeta);
        }
    }
}

MsgHandler ChatService::getHandler(int msgId)
{
    auto it = msgHandlerMap_.find(msgId);
    if (it == msgHandlerMap_.end())
    {
        return [=](const TcpConnectionPtr&, json&, Timestamp)
        {
            LOG_ERROR << "msgId " << msgId << " has no handler.";
        };
    }
    else
    {
        return msgHandlerMap_[msgId];
    }
}

void ChatService::login(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = userModel_.query(id);

    // 先查控制面里的会话归属，而不是只看数据库里的 online/offline。
    // 原因：数据库状态只能说明“曾经在线”，无法说明“当前到底归属哪个活节点”。
    // 要保证“一个在线用户只归属一个节点”，因此登录先看 Redis 路由表。
    string existingNodeId;
    bool hasValidSession = nodeRegistry_ && nodeRegistry_->queryUserNode(id, existingNodeId);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (hasValidSession)
        {
            json resp;
            resp["msgId"] = LOGIN_MSG_ACK;
            resp["errno"] = 2;
            resp["errmsg"] = "This account is using, input another!";
            conn->send(resp.dump());
        }
        else
        {
            if (user.getState() == "online")
            {
                user.setState("offline");
                userModel_.updateState(user);
                ++staleSessionCount_;
            }

            addLocalConnection(id, conn);

            // 登录成功后，把用户归属写入控制面：userId -> nodeId。
            // 为什么要这样做：后续别的节点给该用户发消息时，
            // 第一件事就是查“这个用户现在在哪个节点”。
            if (nodeRegistry_ && !nodeRegistry_->bindUser(id))
            {
                removeLocalConnection(id);

                json resp;
                resp["msgId"] = LOGIN_MSG_ACK;
                resp["errno"] = 3;
                resp["errmsg"] = "register session failed";
                conn->send(resp.dump());
                return;
            }

            user.setState("online");
            userModel_.updateState(user);

            json resp;
            resp["msgId"] = LOGIN_MSG_ACK;
            resp["errno"] = 0;
            resp["id"] = user.getId();
            resp["name"] = user.getName();

            // 查询离线消息
            vector<string> offlineMsgVec = offlineMsgModel_.query(id);
            if (!offlineMsgVec.empty())
            {
                resp["offlinemsg"] = offlineMsgVec;
                offlineMsgModel_.remove(id);
            }

            // 查询好友信息
            vector<User> userVec = friendModel_.query(id);
            if (!userVec.empty())
            {
                vector<string> vec;
                for (auto& user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec.push_back(js.dump());
                }
                resp["friends"] = vec;
            }

            // 查询群组信息
            vector<Group> groupVec = groupModel_.queryGroups(id);
            if (!groupVec.empty())
            {
                vector<string> groupV;
                for (Group& group : groupVec)
                {
                    json grpJson;
                    grpJson["id"] = group.getId();
                    grpJson["groupname"] = group.getName();
                    grpJson["groupdesc"] = group.getDesc();

                    vector<string> userVec;
                    for (GroupUser& user: group.getUsers())
                    {
                        json usrJson;
                        usrJson["id"] = user.getId();
                        usrJson["name"] = user.getName();
                        usrJson["state"] = user.getState();
                        usrJson["role"] = user.getRole();
                        userVec.push_back(usrJson.dump());
                    }

                    grpJson["users"] = userVec;
                    groupV.push_back(grpJson.dump());
                }

                resp["groups"] = groupV;
            }

            conn->send(resp.dump());
        }
    }
    else
    {
        json resp;
        resp["msgId"] = LOGIN_MSG_ACK;
        resp["errno"] = 1;
        resp["errmsg"] = "id or password is invalid!";
        conn->send(resp.dump());
    }
}

void ChatService::reg(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = userModel_.insert(user);
    if (state)
    {
        json resp;
        resp["msgId"] = REG_MSG_ACK;
        resp["errno"] = 0;
        resp["id"] = user.getId();
        conn->send(resp.dump());
    }
    else
    {
        json resp;
        resp["msgId"] = REG_MSG_ACK;
        resp["errno"] = 1;
        conn->send(resp.dump());
    }
}

void ChatService::logout(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();

    removeLocalConnection(userid);

    if (nodeRegistry_)
    {
        nodeRegistry_->unbindUser(userid);
    }

    User user(userid, "", "", "offline");
    userModel_.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn)
{
    User user;

    user.setId(removeLocalConnectionByConn(conn));

    if (user.getId() != -1)
    {
        if (nodeRegistry_)
        {
            nodeRegistry_->unbindUser(user.getId());
        }

        user.setState("offline");
        userModel_.updateState(user);
    }
}

void ChatService::oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    (void)conn;
    (void)time;

    ++oneChatSourceCount_;

    int toId = js["to"].get<int>();
    int fromId = js["id"].get<int>();
    string msg = js.dump();

    // 第一步永远先走本地直发。
    // 原因：本地命中路径最短，不需要查 Redis，也不需要跨节点网络开销。
    if (deliverToLocalUser(toId, msg))
    {
        ++localDeliveryCount_;
        return;
    }

    string nodeId;
    NodeMeta nodeMeta;

    // 第二步查控制面里的 userId -> nodeId。
    // Redis 在这里不再负责“转发消息”，只负责回答“目标用户属于哪个节点”。
    long long resolveStartNs = nowSteadyNs();
    bool resolved = nodeRegistry_ && nodeRegistry_->resolveUserNode(toId, nodeId, nodeMeta);
    oneChatResolveNs_.fetch_add(nowSteadyNs() - resolveStartNs);
    ++oneChatResolveCount_;

    if (resolved)
    {
        if (nodeRegistry_->isSelfNode(nodeId))
        {
            // Redis 说用户在本节点，但本地连接表里又没找到该用户，
            // 这说明控制面留下了脏路由，需要清理掉。
            ++staleSessionCount_;
            nodeRegistry_->unbindUser(toId);
        }
        else
        {
            // 一旦确认了远端节点归属，就顺手做一次异步预热。
            // 即使当前请求马上也要发消息，后续同节点流量也能直接复用热连接。
            if (clusterRouter_)
            {
                clusterRouter_->prewarmNode(nodeMeta);
            }

            // 已知目标用户属于远端节点后，直接走节点间 TCP 直连数据面。
            // 从“共享 Redis 转发”变成“已知地址后的定向直投”。
            long long remoteSendStartNs = nowSteadyNs();
            bool sent = clusterRouter_ &&
                clusterRouter_->sendOneChat(nodeMeta, selfNode_.nodeId, toId, msg, buildTraceId(fromId));
            oneChatRemoteSendNs_.fetch_add(nowSteadyNs() - remoteSendStartNs);
            ++oneChatRemoteSendCount_;
            if (sent)
            {
                ++remoteRouteCount_;
                return;
            }

            ++remoteRouteFailCount_;
        }
    }

    // 既不在本地，也没有有效远端归属，或者远端投递失败，
    // 最终统一降级为离线消息，保证消息不会在热路径里丢失。
    ++offlineFallbackCount_;
    offlineMsgModel_.insert(toId, msg);
}

// 处理服务器异常退出
void ChatService::reset()
{
    vector<int> selfUsers;
    if (nodeRegistry_)
    {
        selfUsers = redis_.getNodeUsers(selfNode_.nodeId);
    }

    if (nodeRegistry_)
    {
        nodeRegistry_->stop();
        nodeRegistry_->cleanupSelf();
    }

    if (clusterRouter_)
    {
        clusterRouter_->closeAll();
    }

    auto avgUs = [](long long totalNs, long long count) -> long long {
        return count > 0 ? totalNs / count / 1000 : 0;
    };

    LOG_INFO << "cluster metrics local=" << localDeliveryCount_.load()
             << " remote=" << remoteRouteCount_.load()
             << " remote_fail=" << remoteRouteFailCount_.load()
             << " offline=" << offlineFallbackCount_.load()
             << " stale_session=" << staleSessionCount_.load();

    LOG_INFO << "oneChat metrics"
             << " source=" << oneChatSourceCount_.load()
             << " resolve_avg_us=" << avgUs(oneChatResolveNs_.load(), oneChatResolveCount_.load())
             << " remote_send_avg_us=" << avgUs(oneChatRemoteSendNs_.load(), oneChatRemoteSendCount_.load())
             << " remote_handle_avg_us=" << avgUs(oneChatRemoteHandleNs_.load(), oneChatRemoteHandleCount_.load())
             << " local_lookup_avg_us=" << avgUs(localLookupNs_.load(), localLookupCount_.load())
             << " local_lookup_miss=" << localLookupMissCount_.load()
             << " local_enqueue_avg_us=" << avgUs(localDispatchEnqueueNs_.load(), localDispatchEnqueueCount_.load())
             << " local_queue_wait_avg_us=" << avgUs(localDispatchQueueWaitNs_.load(), localDispatchDrainMsgCount_.load())
             << " local_queue_wait_max_us=" << (localDispatchQueueWaitMaxNs_.load() / 1000)
             << " local_send_avg_us=" << avgUs(localDispatchSendNs_.load(), localDispatchDrainMsgCount_.load())
             << " local_dispatch_inline=" << localDispatchInlineCount_.load()
             << " local_dispatch_queued=" << localDispatchQueuedCount_.load()
             << " local_dispatch_peak=" << localDispatchPeakQueueDepth_.load();

    if (nodeRegistry_)
    {
        for (int userId : selfUsers)
        {
            User user(userId, "", "", "offline");
            userModel_.updateState(user);
        }
        return;
    }

    userModel_.resetState();
}

void ChatService::addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int friendId = js["friendId"].get<int>();

    friendModel_.insert(userId, friendId);
}

void ChatService::createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    Group group(-1, name, desc);
    if (groupModel_.createGroup(group))
    {
        groupModel_.addGroup(userid, group.getId(), "creator");
    }
}

void ChatService::addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    groupModel_.addGroup(userid, groupid, "normal");
}

void ChatService::groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> userIdVec = groupModel_.queryGroupUsers(userid, groupid);
    if (userIdVec.empty())
    {
        return;
    }

    string msg = js.dump();
    vector<TcpConnectionPtr> localConns;
    vector<int> offlineIds;
    vector<int> unresolvedIds;
    unordered_map<string, vector<int>> remoteNodeUsers;
    localConns.reserve(userIdVec.size());
    unresolvedIds.reserve(userIdVec.size());

    for (int id : userIdVec)
    {
        TcpConnectionPtr localConn = getLocalConnection(id);
        if (localConn)
        {
            localConns.push_back(localConn);
        }
        else
        {
            unresolvedIds.push_back(id);
        }
    }

    // 对不在本地连接表中的用户，再去控制面查归属。
    // 现在连接表已经分片，不再有“一把全局锁包住整段扫描”的问题。
    for (int id : unresolvedIds)
    {
        string nodeId;
        NodeMeta nodeMeta;
        if (nodeRegistry_ && nodeRegistry_->resolveUserNode(id, nodeId, nodeMeta))
        {
            if (nodeRegistry_->isSelfNode(nodeId))
            {
                ++staleSessionCount_;
                nodeRegistry_->unbindUser(id);
                offlineIds.push_back(id);
            }
            else
            {
                remoteNodeUsers[nodeId].push_back(id);
            }
        }
        else
        {
            offlineIds.push_back(id);
        }
    }

    for (const auto& userConn : localConns)
    {
        userConn->send(msg);
        ++localDeliveryCount_;
    }

    // 群聊跨节点按 nodeId 分桶发送，而不是按用户逐个远端发送。
    // 这样做的原因是：跨节点成本按“节点数”计，而不是按“用户数”计。
    if (!remoteNodeUsers.empty())
    {
        for (auto& entry : remoteNodeUsers)
        {
            NodeMeta nodeMeta;
            if (nodeRegistry_ && clusterRouter_ && nodeRegistry_->queryNode(entry.first, nodeMeta))
            {
                clusterRouter_->prewarmNode(nodeMeta);
                if (clusterRouter_->sendGroupChat(nodeMeta, selfNode_.nodeId, entry.second, msg, buildTraceId(userid)))
                {
                    ++remoteRouteCount_;
                    continue;
                }
            }

            ++remoteRouteFailCount_;
            offlineIds.insert(offlineIds.end(), entry.second.begin(), entry.second.end());
        }
    }

    for (int id : offlineIds)
    {
        ++offlineFallbackCount_;
        offlineMsgModel_.insert(id, msg);
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    if (deliverToLocalUser(userid, msg))
    {
        ++localDeliveryCount_;
        return;
    }

    ++offlineFallbackCount_;
    offlineMsgModel_.insert(userid, msg);
}

void ChatService::handleClusterMessage(json& js)
{
    // 内部消息和客户端消息是两套协议：
    // 客户端消息负责聊天业务，内部消息负责节点间路由投递。
    int msgId = js["msgId"].get<int>();
    if (msgId == NODE_ROUTE_ONE_CHAT_MSG)
    {
        handleClusterOneChat(js);
        return;
    }

    if (msgId == NODE_ROUTE_GROUP_CHAT_MSG)
    {
        handleClusterGroupChat(js);
        return;
    }

    LOG_ERROR << "unknown cluster msgId: " << msgId;
}

size_t ChatService::pickConnBucket(int userId) const
{
    if (connBucketCount_ == 0)
    {
        return 0;
    }

    return static_cast<size_t>(userId >= 0 ? userId : -userId) % connBucketCount_;
}

void ChatService::addLocalConnection(int userId, const TcpConnectionPtr& conn)
{
    ConnectionBucket* bucket = connBuckets_[pickConnBucket(userId)].get();
    lock_guard<mutex> lock(bucket->bucketMutex);
    bucket->userConnMap[userId] = conn;
}

void ChatService::removeLocalConnection(int userId)
{
    ConnectionBucket* bucket = connBuckets_[pickConnBucket(userId)].get();
    lock_guard<mutex> lock(bucket->bucketMutex);
    bucket->userConnMap.erase(userId);
}

int ChatService::removeLocalConnectionByConn(const TcpConnectionPtr& conn)
{
    for (size_t i = 0; i < connBuckets_.size(); ++i)
    {
        ConnectionBucket* bucket = connBuckets_[i].get();
        lock_guard<mutex> lock(bucket->bucketMutex);
        for (auto it = bucket->userConnMap.begin(); it != bucket->userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                int userId = it->first;
                bucket->userConnMap.erase(it);
                return userId;
            }
        }
    }

    return -1;
}

TcpConnectionPtr ChatService::getLocalConnection(int userId)
{
    ConnectionBucket* bucket = connBuckets_[pickConnBucket(userId)].get();
    lock_guard<mutex> lock(bucket->bucketMutex);
    auto it = bucket->userConnMap.find(userId);
    if (it == bucket->userConnMap.end())
    {
        return TcpConnectionPtr();
    }

    return it->second;
}

shared_ptr<ChatService::LocalLoopDispatcher> ChatService::getOrCreateLocalDispatcher(EventLoop* loop)
{
    lock_guard<mutex> lock(localDispatchersMutex_);
    auto it = localDispatchers_.find(loop);
    if (it != localDispatchers_.end())
    {
        return it->second;
    }

    shared_ptr<LocalLoopDispatcher> dispatcher(new LocalLoopDispatcher(loop, localDeliveryQueueLimit_));
    localDispatchers_[loop] = dispatcher;
    return dispatcher;
}

void ChatService::drainLocalDeliveriesInLoop(const shared_ptr<LocalLoopDispatcher>& dispatcher)
{
    dispatcher->loop->assertInLoopThread();

    while (true)
    {
        deque<LocalDeliveryTask> tasks;
        {
            lock_guard<mutex> lock(dispatcher->pendingMutex);
            if (dispatcher->pending.empty())
            {
                dispatcher->flushScheduled = false;
                return;
            }

            tasks.swap(dispatcher->pending);
        }

        while (!tasks.empty())
        {
            LocalDeliveryTask task = tasks.front();
            tasks.pop_front();

            long long drainStartNs = nowSteadyNs();
            long long queueWaitNs = drainStartNs - task.enqueueNs;
            localDispatchQueueWaitNs_.fetch_add(queueWaitNs);
            updatePeak(localDispatchQueueWaitMaxNs_, queueWaitNs);

            task.conn->send(task.msg);
            localDispatchSendNs_.fetch_add(nowSteadyNs() - drainStartNs);
            ++localDispatchDrainMsgCount_;
        }
    }
}

bool ChatService::enqueueLocalDelivery(const TcpConnectionPtr& conn, const string& msg)
{
    long long enqueueStartNs = nowSteadyNs();
    shared_ptr<LocalLoopDispatcher> dispatcher = getOrCreateLocalDispatcher(conn->getLoop());
    bool shouldSchedule = false;

    {
        lock_guard<mutex> lock(dispatcher->pendingMutex);
        if (dispatcher->pending.size() >= dispatcher->pendingLimit)
        {
            LOG_WARN << "local delivery queue full loop=" << conn->getLoop()
                     << " size=" << dispatcher->pending.size();
            return false;
        }

        dispatcher->pending.push_back(LocalDeliveryTask{conn, msg, nowSteadyNs()});
        updatePeak(localDispatchPeakQueueDepth_, static_cast<long long>(dispatcher->pending.size()));
        if (!dispatcher->flushScheduled)
        {
            dispatcher->flushScheduled = true;
            shouldSchedule = true;
        }
    }

    localDispatchEnqueueNs_.fetch_add(nowSteadyNs() - enqueueStartNs);
    ++localDispatchEnqueueCount_;

    if (shouldSchedule)
    {
        if (dispatcher->loop->isInLoopThread())
        {
            ++localDispatchInlineCount_;
            drainLocalDeliveriesInLoop(dispatcher);
        }
        else
        {
            ++localDispatchQueuedCount_;
            dispatcher->loop->queueInLoop([this, dispatcher]() {
                drainLocalDeliveriesInLoop(dispatcher);
            });
        }
    }

    return true;
}

bool ChatService::deliverToLocalUser(int userId, const string& msg)
{
    long long lookupStartNs = nowSteadyNs();
    TcpConnectionPtr conn = getLocalConnection(userId);
    localLookupNs_.fetch_add(nowSteadyNs() - lookupStartNs);
    ++localLookupCount_;
    if (!conn)
    {
        ++localLookupMissCount_;
        return false;
    }

    return enqueueLocalDelivery(conn, msg);
}

string ChatService::buildTraceId(int fromUserId) const
{
    long long nowMs = chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
    ostringstream oss;
    oss << selfNode_.nodeId << '-' << fromUserId << '-' << nowMs;
    return oss.str();
}

void ChatService::handleClusterOneChat(json& js)
{
    int toUser = js["toUser"].get<int>();
    string payload = js["payload"];
    handleClusterOneChatFrame(toUser, payload);
}

void ChatService::handleClusterOneChatFrame(int toUser, const string& payload)
{
    long long remoteHandleStartNs = nowSteadyNs();
    if (deliverToLocalUser(toUser, payload))
    {
        ++oneChatRemoteHandleCount_;
        oneChatRemoteHandleNs_.fetch_add(nowSteadyNs() - remoteHandleStartNs);
        ++localDeliveryCount_;
        return;
    }

    ++oneChatRemoteHandleCount_;
    oneChatRemoteHandleNs_.fetch_add(nowSteadyNs() - remoteHandleStartNs);

    if (nodeRegistry_)
    {
        string nodeId;
        if (nodeRegistry_->queryUserNode(toUser, nodeId) && !nodeRegistry_->isSelfNode(nodeId))
        {
            ++staleSessionCount_;
        }
        else
        {
            nodeRegistry_->unbindUser(toUser);
        }
    }

    ++offlineFallbackCount_;
    offlineMsgModel_.insert(toUser, payload);
}

void ChatService::handleClusterGroupChat(json& js)
{
    vector<int> toUsers = js["toUsers"].get<vector<int>>();
    string payload = js["payload"];

    for (int userId : toUsers)
    {
        if (deliverToLocalUser(userId, payload))
        {
            ++localDeliveryCount_;
            continue;
        }

        if (nodeRegistry_)
        {
            string nodeId;
            if (nodeRegistry_->queryUserNode(userId, nodeId) && !nodeRegistry_->isSelfNode(nodeId))
            {
                ++staleSessionCount_;
            }
            else
            {
                nodeRegistry_->unbindUser(userId);
            }
        }

        ++offlineFallbackCount_;
        offlineMsgModel_.insert(userId, payload);
    }
}
