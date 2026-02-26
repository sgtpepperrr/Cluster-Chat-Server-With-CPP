#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>

using namespace muduo;
using namespace std;

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
    int toId = js["to"].get<int>();

    {
        lock_guard<mutex> lock(connMutex_);

        auto it = userConnMap_.find(toId);
        if (it != userConnMap_.end())
        {
            // toId在线且位于当前服务器，转发消息
            it->second->send(js.dump());
            return;
        }
    }

    // 查询toid是否在其它服务器登录
    User user = userModel_.query(toId);
    if (user.getState() == "online")
    {
        redis_.publish(toId, js.dump());
        return;
    }

    // toId不在线，存储离线消息
    offlineMsgModel_.insert(toId, js.dump());
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
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> userIdVec = groupModel_.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(connMutex_);
    for (int id : userIdVec)
    {
        auto it = userConnMap_.find(id);
        if (it != userConnMap_.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // id不在当前服务器，查询是否在其它服务器登录了
            User user = userModel_.query(id);
            if (user.getState() == "online")
            {
                redis_.publish(id, js.dump());
            }
            else
            {
                // 存储离线消息
                offlineMsgModel_.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(connMutex_);
    auto it = userConnMap_.find(userid);
    if (it != userConnMap_.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    offlineMsgModel_.insert(userid, msg);
}
