#include "chatservice.hpp"
#include "config.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

using namespace muduo;
using namespace std;

namespace
{
using steady_clock = std::chrono::steady_clock;
using microseconds = std::chrono::microseconds;

uint64_t nowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

double avg(uint64_t total, uint64_t count)
{
    if (count == 0)
    {
        return 0.0;
    }
    return static_cast<double>(total) / static_cast<double>(count);
}

double pct(uint64_t part, uint64_t total)
{
    if (total == 0)
    {
        return 0.0;
    }
    return static_cast<double>(part) * 100.0 / static_cast<double>(total);
}

bool perfTraceEnabled()
{
    static bool enabled = []() {
        loadEnvFile();
        return getEnvIntOrDefault("CHAT_PERF_TRACE", 0) > 0;
    }();
    return enabled;
}

uint64_t perfReportEvery()
{
    static uint64_t every = []() {
        int raw = getEnvIntOrDefault("CHAT_PERF_REPORT_EVERY", 2000);
        if (raw <= 0)
        {
            raw = 2000;
        }
        return static_cast<uint64_t>(raw);
    }();
    return every;
}

std::atomic<uint64_t> g_oneMsgCount(0);
std::atomic<uint64_t> g_oneLockWaitUs(0);
std::atomic<uint64_t> g_oneLockHoldUs(0);
std::atomic<uint64_t> g_oneLocalSendCount(0);
std::atomic<uint64_t> g_oneLocalSendUs(0);
std::atomic<uint64_t> g_oneRedisPublishCount(0);
std::atomic<uint64_t> g_oneRedisPublishUs(0);
std::atomic<uint64_t> g_oneRedisNoSubscriberCount(0);
std::atomic<uint64_t> g_oneRedisFailCount(0);
std::atomic<uint64_t> g_oneOfflineInsertCount(0);
std::atomic<uint64_t> g_oneOfflineInsertUs(0);

std::atomic<uint64_t> g_groupMsgCount(0);
std::atomic<uint64_t> g_groupQueryUsersUs(0);
std::atomic<uint64_t> g_groupReceiverCount(0);
std::atomic<uint64_t> g_groupLockWaitUs(0);
std::atomic<uint64_t> g_groupLockHoldUs(0);
std::atomic<uint64_t> g_groupLocalSendCount(0);
std::atomic<uint64_t> g_groupLocalSendUs(0);
std::atomic<uint64_t> g_groupRedisPublishCount(0);
std::atomic<uint64_t> g_groupRedisPublishUs(0);
std::atomic<uint64_t> g_groupRedisNoSubscriberCount(0);
std::atomic<uint64_t> g_groupRedisFailCount(0);
std::atomic<uint64_t> g_groupOfflineInsertCount(0);
std::atomic<uint64_t> g_groupOfflineInsertUs(0);

void reportOneChatPerf(uint64_t msgCount)
{
    uint64_t localCount = g_oneLocalSendCount.load(std::memory_order_relaxed);
    uint64_t redisCount = g_oneRedisPublishCount.load(std::memory_order_relaxed);
    uint64_t offlineCount = g_oneOfflineInsertCount.load(std::memory_order_relaxed);

    LOG_INFO << "[perf][oneChat] msgs=" << msgCount
             << " local_hit=" << pct(localCount, msgCount) << "%"
             << " redis_path=" << pct(redisCount, msgCount) << "%"
             << " offline_path=" << pct(offlineCount, msgCount) << "%"
             << " avg_lock_wait_us="
             << avg(g_oneLockWaitUs.load(std::memory_order_relaxed), msgCount)
             << " avg_lock_hold_us="
             << avg(g_oneLockHoldUs.load(std::memory_order_relaxed), msgCount)
             << " avg_local_send_us="
             << avg(g_oneLocalSendUs.load(std::memory_order_relaxed), localCount)
             << " avg_redis_pub_us="
             << avg(g_oneRedisPublishUs.load(std::memory_order_relaxed), redisCount)
             << " avg_offline_insert_us="
             << avg(g_oneOfflineInsertUs.load(std::memory_order_relaxed), offlineCount)
             << " redis_no_subscribers=" << g_oneRedisNoSubscriberCount.load(std::memory_order_relaxed)
             << " redis_publish_fail=" << g_oneRedisFailCount.load(std::memory_order_relaxed);
}

void reportGroupChatPerf(uint64_t msgCount)
{
    uint64_t receivers = g_groupReceiverCount.load(std::memory_order_relaxed);
    uint64_t localCount = g_groupLocalSendCount.load(std::memory_order_relaxed);
    uint64_t redisCount = g_groupRedisPublishCount.load(std::memory_order_relaxed);
    uint64_t offlineCount = g_groupOfflineInsertCount.load(std::memory_order_relaxed);

    LOG_INFO << "[perf][groupChat] msgs=" << msgCount
             << " avg_receivers_per_msg=" << avg(receivers, msgCount)
             << " local_receiver_ratio=" << pct(localCount, receivers) << "%"
             << " redis_receiver_ratio=" << pct(redisCount, receivers) << "%"
             << " offline_receiver_ratio=" << pct(offlineCount, receivers) << "%"
             << " avg_query_users_us="
             << avg(g_groupQueryUsersUs.load(std::memory_order_relaxed), msgCount)
             << " avg_lock_wait_us="
             << avg(g_groupLockWaitUs.load(std::memory_order_relaxed), msgCount)
             << " avg_lock_hold_us="
             << avg(g_groupLockHoldUs.load(std::memory_order_relaxed), msgCount)
             << " avg_local_send_us="
             << avg(g_groupLocalSendUs.load(std::memory_order_relaxed), localCount)
             << " avg_redis_pub_us="
             << avg(g_groupRedisPublishUs.load(std::memory_order_relaxed), redisCount)
             << " avg_offline_insert_us="
             << avg(g_groupOfflineInsertUs.load(std::memory_order_relaxed), offlineCount)
             << " redis_no_subscribers="
             << g_groupRedisNoSubscriberCount.load(std::memory_order_relaxed)
             << " redis_publish_fail="
             << g_groupRedisFailCount.load(std::memory_order_relaxed);
}
} // namespace

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

ChatService::ChatService()
{
    msgHandlerMap_.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    msgHandlerMap_.insert({LOGOUT_MSG, std::bind(&ChatService::logout, this, _1, _2, _3)});
    msgHandlerMap_.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    msgHandlerMap_.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    msgHandlerMap_.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (redis_.connect())
    {
        // 设置上报消息的回调
        redis_.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
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
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            json resp;
            resp["msgId"] = LOGIN_MSG_ACK;
            resp["errno"] = 2;
            resp["errmsg"] = "This account is using, input another!";
            conn->send(resp.dump());
        }
        else
        {
            // 登录成功
            {
                lock_guard<mutex> lock(connMutex_);
                userConnMap_.insert({id, conn});
            }

            redis_.subscribe(id);

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

    {
        lock_guard<mutex> lock(connMutex_);
        auto it = userConnMap_.find(userid);
        if (it != userConnMap_.end())
        {
            userConnMap_.erase(it);
        }
    }

    redis_.unsubscribe(userid);

    User user(userid, "", "", "offline");
    userModel_.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn)
{
    User user;

    {
        lock_guard<mutex> lock(connMutex_);

        for (auto it = userConnMap_.begin(); it != userConnMap_.end(); ++it)
        {
            if (it->second == conn)
            {
                user.setId(it->first);
                userConnMap_.erase(it);
                break;
            }
        }
    }

    redis_.unsubscribe(user.getId());

    if (user.getId() != -1)
    {
        user.setState("offline");
        userModel_.updateState(user);
    }
}

void ChatService::oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    bool perf = perfTraceEnabled();
    int toId = js["to"].get<int>();
    string msg = js.dump();
    TcpConnectionPtr targetConn;
    uint64_t lockWaitUs = 0;
    uint64_t lockHoldUs = 0;
    uint64_t localSendUs = 0;
    uint64_t redisPublishUs = 0;
    uint64_t offlineInsertUs = 0;
    bool localPath = false;
    bool redisPath = false;
    bool redisNoSubscriber = false;
    bool redisPublishFail = false;
    bool offlinePath = false;
    uint64_t waitStartUs = 0;
    uint64_t lockAcquiredUs = 0;
    if (perf)
    {
        waitStartUs = nowUs();
    }

    {
        unique_lock<mutex> lock(connMutex_);
        if (perf)
        {
            lockAcquiredUs = nowUs();
            lockWaitUs = lockAcquiredUs - waitStartUs;
        }

        auto it = userConnMap_.find(toId);
        if (it != userConnMap_.end())
        {
            targetConn = it->second;
        }

        if (perf)
        {
            lockHoldUs = nowUs() - lockAcquiredUs;
        }
    }

    if (targetConn)
    {
        // toId在线且位于当前服务器，转发消息
        localPath = true;
        uint64_t localSendStartUs = 0;
        if (perf)
        {
            localSendStartUs = nowUs();
        }
        targetConn->send(msg);
        if (perf)
        {
            localSendUs = nowUs() - localSendStartUs;
        }
    }
    else
    {
        redisPath = true;
        uint64_t redisPublishStartUs = 0;
        if (perf)
        {
            redisPublishStartUs = nowUs();
        }

        // 尝试通过Redis投递到其它节点。若无订阅者（或发布失败）则转离线消息。
        int subscribers = redis_.publish(toId, msg);
        if (perf)
        {
            redisPublishUs = nowUs() - redisPublishStartUs;
        }

        if (subscribers <= 0)
        {
            redisNoSubscriber = (subscribers == 0);
            redisPublishFail = (subscribers < 0);
            offlinePath = true;

            // toId不在线，存储离线消息
            uint64_t offlineInsertStartUs = 0;
            if (perf)
            {
                offlineInsertStartUs = nowUs();
            }
            offlineMsgModel_.insert(toId, msg);
            if (perf)
            {
                offlineInsertUs = nowUs() - offlineInsertStartUs;
            }
        }
    }

    if (!perf)
    {
        return;
    }

    uint64_t msgCount = g_oneMsgCount.fetch_add(1, std::memory_order_relaxed) + 1;
    g_oneLockWaitUs.fetch_add(lockWaitUs, std::memory_order_relaxed);
    g_oneLockHoldUs.fetch_add(lockHoldUs, std::memory_order_relaxed);

    if (localPath)
    {
        g_oneLocalSendCount.fetch_add(1, std::memory_order_relaxed);
        g_oneLocalSendUs.fetch_add(localSendUs, std::memory_order_relaxed);
    }

    if (redisPath)
    {
        g_oneRedisPublishCount.fetch_add(1, std::memory_order_relaxed);
        g_oneRedisPublishUs.fetch_add(redisPublishUs, std::memory_order_relaxed);
    }

    if (redisNoSubscriber)
    {
        g_oneRedisNoSubscriberCount.fetch_add(1, std::memory_order_relaxed);
    }

    if (redisPublishFail)
    {
        g_oneRedisFailCount.fetch_add(1, std::memory_order_relaxed);
    }

    if (offlinePath)
    {
        g_oneOfflineInsertCount.fetch_add(1, std::memory_order_relaxed);
        g_oneOfflineInsertUs.fetch_add(offlineInsertUs, std::memory_order_relaxed);
    }

    if (msgCount % perfReportEvery() == 0)
    {
        reportOneChatPerf(msgCount);
    }
}

// 处理服务器异常退出
void ChatService::reset()
{
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
    bool perf = perfTraceEnabled();
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    uint64_t queryUsersUs = 0;
    if (perf)
    {
        uint64_t queryStartUs = nowUs();
        vector<int> tmp = groupModel_.queryGroupUsers(userid, groupid);
        queryUsersUs = nowUs() - queryStartUs;
        if (tmp.empty())
        {
            return;
        }

        vector<int> userIdVec = tmp;
        string msg = js.dump();
        vector<TcpConnectionPtr> localConns;
        vector<int> otherNodeOrOfflineIds;
        localConns.reserve(userIdVec.size());
        otherNodeOrOfflineIds.reserve(userIdVec.size());

        uint64_t lockWaitUs = 0;
        uint64_t lockHoldUs = 0;
        uint64_t waitStartUs = nowUs();
        {
            unique_lock<mutex> lock(connMutex_);
            uint64_t lockAcquiredUs = nowUs();
            lockWaitUs = lockAcquiredUs - waitStartUs;
            for (int id : userIdVec)
            {
                auto it = userConnMap_.find(id);
                if (it != userConnMap_.end())
                {
                    localConns.push_back(it->second);
                }
                else
                {
                    otherNodeOrOfflineIds.push_back(id);
                }
            }
            lockHoldUs = nowUs() - lockAcquiredUs;
        }

        uint64_t localSendUs = 0;
        for (const auto& userConn : localConns)
        {
            uint64_t sendStartUs = nowUs();
            userConn->send(msg);
            localSendUs += (nowUs() - sendStartUs);
        }

        uint64_t redisPublishUs = 0;
        uint64_t offlineInsertUs = 0;
        uint64_t redisNoSubscriberCount = 0;
        uint64_t redisFailCount = 0;
        uint64_t offlineInsertCount = 0;
        for (int id : otherNodeOrOfflineIds)
        {
            uint64_t redisStartUs = nowUs();
            int subscribers = redis_.publish(id, msg);
            redisPublishUs += (nowUs() - redisStartUs);
            if (subscribers > 0)
            {
                continue;
            }

            if (subscribers == 0)
            {
                ++redisNoSubscriberCount;
            }
            else
            {
                ++redisFailCount;
            }

            ++offlineInsertCount;
            uint64_t offlineStartUs = nowUs();
            offlineMsgModel_.insert(id, msg);
            offlineInsertUs += (nowUs() - offlineStartUs);
        }

        uint64_t msgCount = g_groupMsgCount.fetch_add(1, std::memory_order_relaxed) + 1;
        g_groupQueryUsersUs.fetch_add(queryUsersUs, std::memory_order_relaxed);
        g_groupReceiverCount.fetch_add(userIdVec.size(), std::memory_order_relaxed);
        g_groupLockWaitUs.fetch_add(lockWaitUs, std::memory_order_relaxed);
        g_groupLockHoldUs.fetch_add(lockHoldUs, std::memory_order_relaxed);
        g_groupLocalSendCount.fetch_add(localConns.size(), std::memory_order_relaxed);
        g_groupLocalSendUs.fetch_add(localSendUs, std::memory_order_relaxed);
        g_groupRedisPublishCount.fetch_add(otherNodeOrOfflineIds.size(), std::memory_order_relaxed);
        g_groupRedisPublishUs.fetch_add(redisPublishUs, std::memory_order_relaxed);
        g_groupRedisNoSubscriberCount.fetch_add(redisNoSubscriberCount, std::memory_order_relaxed);
        g_groupRedisFailCount.fetch_add(redisFailCount, std::memory_order_relaxed);
        g_groupOfflineInsertCount.fetch_add(offlineInsertCount, std::memory_order_relaxed);
        g_groupOfflineInsertUs.fetch_add(offlineInsertUs, std::memory_order_relaxed);

        if (msgCount % perfReportEvery() == 0)
        {
            reportGroupChatPerf(msgCount);
        }

        return;
    }

    vector<int> userIdVec = groupModel_.queryGroupUsers(userid, groupid);
    if (userIdVec.empty())
    {
        return;
    }

    string msg = js.dump();
    vector<TcpConnectionPtr> localConns;
    vector<int> otherNodeOrOfflineIds;
    localConns.reserve(userIdVec.size());
    otherNodeOrOfflineIds.reserve(userIdVec.size());

    {
        lock_guard<mutex> lock(connMutex_);
        for (int id : userIdVec)
        {
            auto it = userConnMap_.find(id);
            if (it != userConnMap_.end())
            {
                localConns.push_back(it->second);
            }
            else
            {
                otherNodeOrOfflineIds.push_back(id);
            }
        }
    }

    for (const auto& userConn : localConns)
    {
        userConn->send(msg);
    }

    if (otherNodeOrOfflineIds.empty())
    {
        return;
    }

    for (int id : otherNodeOrOfflineIds)
    {
        int subscribers = redis_.publish(id, msg);
        if (subscribers > 0)
        {
            continue;
        }

        offlineMsgModel_.insert(id, msg);
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    TcpConnectionPtr conn;
    {
        lock_guard<mutex> lock(connMutex_);
        auto it = userConnMap_.find(userid);
        if (it != userConnMap_.end())
        {
            conn = it->second;
        }
    }

    if (conn)
    {
        conn->send(msg);
        return;
    }

    // 存储该用户的离线消息
    offlineMsgModel_.insert(userid, msg);
}
