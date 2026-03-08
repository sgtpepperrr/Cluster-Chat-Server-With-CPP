#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <deque>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>
#include "json.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
#include "redis.hpp"
#include "clustertypes.hpp"

using namespace std;
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

using MsgHandler = std::function<void(const TcpConnectionPtr&, json&, Timestamp)>;

class ChatService
{
public:
    static ChatService* instance();

    void initNode(EventLoop* baseLoop,
                  const string& clientIp,
                  uint16_t clientPort,
                  const string& interIp,
                  uint16_t interPort);
    
    void login(const TcpConnectionPtr& conn, json& js, Timestamp time);
    void logout(const TcpConnectionPtr& conn, json& js, Timestamp time);
    void reg(const TcpConnectionPtr& conn, json& js, Timestamp time);

    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    void addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    void groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time);

    MsgHandler getHandler(int msgId);

    // 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr& conn);

    // 处理服务器异常退出
    void reset();

    // 从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int, string);

    void handleClusterMessage(json& js);
    void handleClusterOneChatFrame(int toUser, const string& payload);

private:
    struct ConnectionBucket
    {
        unordered_map<int, TcpConnectionPtr> userConnMap;
        mutex bucketMutex;
    };

    struct LocalDeliveryTask
    {
        TcpConnectionPtr conn;
        string msg;
    };

    struct LocalLoopDispatcher
    {
        LocalLoopDispatcher(EventLoop* ownerLoop, size_t limit)
            : loop(ownerLoop)
            , flushScheduled(false)
            , pendingLimit(limit)
        {
        }

        EventLoop* loop;
        deque<LocalDeliveryTask> pending;
        bool flushScheduled;
        size_t pendingLimit;
        mutex pendingMutex;
    };

    ChatService();

    size_t pickConnBucket(int userId) const;
    void addLocalConnection(int userId, const TcpConnectionPtr& conn);
    void removeLocalConnection(int userId);
    int removeLocalConnectionByConn(const TcpConnectionPtr& conn);
    TcpConnectionPtr getLocalConnection(int userId);
    shared_ptr<LocalLoopDispatcher> getOrCreateLocalDispatcher(EventLoop* loop);
    void drainLocalDeliveriesInLoop(const shared_ptr<LocalLoopDispatcher>& dispatcher);
    bool enqueueLocalDelivery(const TcpConnectionPtr& conn, const string& msg);
    bool deliverToLocalUser(int userId, const string& msg);
    string buildTraceId(int fromUserId) const;
    void handleClusterOneChat(json& js);
    void handleClusterGroupChat(json& js);

    unordered_map<int, MsgHandler> msgHandlerMap_;

    // 在线连接表按 userId 分片，避免所有本地投递都抢同一把全局锁。
    vector<unique_ptr<ConnectionBucket> > connBuckets_;
    size_t connBucketCount_;

    // 本地消息按“目标连接所属 EventLoop”分桶，减少每条消息单独跨线程调度。
    mutex localDispatchersMutex_;
    unordered_map<EventLoop*, shared_ptr<LocalLoopDispatcher> > localDispatchers_;
    size_t localDeliveryQueueLimit_;

    UserModel userModel_;

    OfflineMsgModel offlineMsgModel_;

    FriendModel friendModel_;

    GroupModel groupModel_;

    Redis redis_;

    unique_ptr<class NodeRegistry> nodeRegistry_;
    unique_ptr<class ClusterRouter> clusterRouter_;
    NodeMeta selfNode_;

    atomic<long long> localDeliveryCount_;
    atomic<long long> remoteRouteCount_;
    atomic<long long> remoteRouteFailCount_;
    atomic<long long> offlineFallbackCount_;
    atomic<long long> staleSessionCount_;
};

#endif /* CHATSERVICE_H */
