#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
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

private:
    ChatService();

    TcpConnectionPtr getLocalConnection(int userId);
    bool deliverToLocalUser(int userId, const string& msg);
    string buildTraceId(int fromUserId) const;
    void handleClusterOneChat(json& js);
    void handleClusterGroupChat(json& js);

    unordered_map<int, MsgHandler> msgHandlerMap_;

    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> userConnMap_;

    mutex connMutex_;

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
