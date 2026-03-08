#include "redis.hpp"
#include "config.hpp"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <poll.h>

using namespace std;

Redis::Redis()
    : publish_ctx_(nullptr)
    , subscribe_ctx_(nullptr)
    , command_ctx_(nullptr)
{
}

Redis::~Redis()
{
    if (publish_ctx_ != nullptr)
    {
        redisFree(publish_ctx_);
    }

    if (subscribe_ctx_ != nullptr)
    {
        redisFree(subscribe_ctx_);
    }

    if (command_ctx_ != nullptr)
    {
        redisFree(command_ctx_);
    }
}

bool Redis::connect()
{
    loadEnvFile();
    string redisHost = getEnvOrDefault("CHAT_REDIS_HOST", "127.0.0.1");
    int redisPort = getEnvIntOrDefault("CHAT_REDIS_PORT", 6379);

    publish_ctx_ = redisConnect(redisHost.c_str(), redisPort);
    if (nullptr == publish_ctx_)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    subscribe_ctx_ = redisConnect(redisHost.c_str(), redisPort);
    if (nullptr == subscribe_ctx_)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    command_ctx_ = redisConnect(redisHost.c_str(), redisPort);
    if (nullptr == command_ctx_)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 在单独的线程中，监听通道上的事件，给业务层进行上报消息
    thread t([&]() { observe_channel_message(); });
    t.detach();

    cout << "connect redis-server success!" << endl;
    return true;
}

int Redis::publish(int channel, const string& message)
{
    lock_guard<mutex> lock(publish_mtx_);

    redisReply* reply = (redisReply*)redisCommand(publish_ctx_, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return -1;
    }

    int subscribers = static_cast<int>(reply->integer);
    freeReplyObject(reply);
    return subscribers;
}

bool Redis::subscribe(int channel)
{
    lock_guard<mutex> lock(cmd_queue_mtx_);
    cmd_queue_.push("SUBSCRIBE " + to_string(channel));
    return true;
}

bool Redis::unsubscribe(int channel)
{
    lock_guard<mutex> lock(cmd_queue_mtx_);
    cmd_queue_.push("UNSUBSCRIBE " + to_string(channel));
    return true;
}

// 在独立线程中接收订阅通道中的消息
void Redis::observe_channel_message()
{
    redisReply* reply = nullptr;

    while (true)
    {
        // 1) 取出队列中的待发命令，通过 subscribe_ctx_ 发送
        {
            lock_guard<mutex> lock(cmd_queue_mtx_);
            while (!cmd_queue_.empty())
            {
                redisAppendCommand(subscribe_ctx_, cmd_queue_.front().c_str());
                cmd_queue_.pop();
            }
        }

        // 刷出输出缓冲区
        int done = 0;
        while (!done)
        {
            if (REDIS_ERR == redisBufferWrite(subscribe_ctx_, &done))
            {
                cerr << "observe: redisBufferWrite error" << endl;
                return;
            }
        }

        // 2) poll 等待 Redis 推送消息，最多等 200ms
        struct pollfd pfd;
        pfd.fd = subscribe_ctx_->fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 200);
        if (ret < 0)
        {
            cerr << "observe: poll error" << endl;
            return;
        }
        if (ret == 0)
        {
            continue;  // 超时，回到第 1 步检查队列
        }

        // 3) 有数据到达，读入内部缓冲区
        if (REDIS_ERR == redisBufferRead(subscribe_ctx_))
        {
            cerr << "observe: redisBufferRead error" << endl;
            return;
        }

        // 4) 从缓冲区中解析所有完整的回复
        while (REDIS_OK == redisGetReplyFromReader(subscribe_ctx_, (void**)&reply))
        {
            if (reply == nullptr)
            {
                break;  // 没有更多完整回复了
            }

            // 订阅收到的消息是一个带三元素的数组
            if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
            {
                notify_message_handler_(atoi(reply->element[1]->str), reply->element[2]->str);
            }

            freeReplyObject(reply);
        }
    }

    cerr << ">>>>>>>>>>>>> observe_channel_message quit <<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int,string)> fn)
{
    this->notify_message_handler_ = fn;
}

bool Redis::registerNode(const NodeMeta& nodeMeta, int ttlSeconds)
{
    lock_guard<mutex> lock(command_mtx_);

    long long heartbeatMs = nodeMeta.heartbeatMs;
    if (heartbeatMs <= 0)
    {
        heartbeatMs = static_cast<long long>(time(nullptr)) * 1000;
    }

    string key = "chat:node:" + nodeMeta.nodeId;
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_,
        "HMSET %s node_id %s instance_id %s client_ip %s client_port %d inter_ip %s inter_port %d heartbeat_ms %lld",
        key.c_str(),
        nodeMeta.nodeId.c_str(),
        nodeMeta.instanceId.c_str(),
        nodeMeta.clientIp.c_str(),
        static_cast<int>(nodeMeta.clientPort),
        nodeMeta.interIp.c_str(),
        static_cast<int>(nodeMeta.interPort),
        heartbeatMs));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(command_ctx_, "EXPIRE %s %d", key.c_str(), ttlSeconds));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SADD chat:nodes %s", nodeMeta.nodeId.c_str()));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::unregisterNode(const string& nodeId)
{
    lock_guard<mutex> lock(command_mtx_);

    string nodeKey = "chat:node:" + nodeId;
    string nodeUsersKey = "chat:node-users:" + nodeId;

    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "DEL %s", nodeKey.c_str()));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(command_ctx_, "DEL %s", nodeUsersKey.c_str()));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);

    reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SREM chat:nodes %s", nodeId.c_str()));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::getNode(const string& nodeId, NodeMeta& nodeMeta)
{
    lock_guard<mutex> lock(command_mtx_);

    string key = "chat:node:" + nodeId;
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "HGETALL %s", key.c_str()));
    if (reply == nullptr)
    {
        return false;
    }

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0)
    {
        freeReplyObject(reply);
        return false;
    }

    NodeMeta meta;
    for (size_t i = 0; i + 1 < reply->elements; i += 2)
    {
        string field = reply->element[i]->str == nullptr ? "" : reply->element[i]->str;
        string value = reply->element[i + 1]->str == nullptr ? "" : reply->element[i + 1]->str;
        if (field == "node_id")
        {
            meta.nodeId = value;
        }
        else if (field == "instance_id")
        {
            meta.instanceId = value;
        }
        else if (field == "client_ip")
        {
            meta.clientIp = value;
        }
        else if (field == "client_port")
        {
            meta.clientPort = static_cast<uint16_t>(atoi(value.c_str()));
        }
        else if (field == "inter_ip")
        {
            meta.interIp = value;
        }
        else if (field == "inter_port")
        {
            meta.interPort = static_cast<uint16_t>(atoi(value.c_str()));
        }
        else if (field == "heartbeat_ms")
        {
            meta.heartbeatMs = atoll(value.c_str());
        }
    }

    freeReplyObject(reply);

    if (!meta.valid())
    {
        return false;
    }

    nodeMeta = meta;
    return true;
}

vector<string> Redis::getNodeIds()
{
    lock_guard<mutex> lock(command_mtx_);

    vector<string> nodeIds;
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SMEMBERS chat:nodes"));
    if (reply == nullptr)
    {
        return nodeIds;
    }

    if (reply->type == REDIS_REPLY_ARRAY)
    {
        nodeIds.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i)
        {
            if (reply->element[i] != nullptr && reply->element[i]->str != nullptr)
            {
                nodeIds.push_back(reply->element[i]->str);
            }
        }
    }

    freeReplyObject(reply);
    return nodeIds;
}

bool Redis::bindUserNode(int userId, const string& nodeId, const string& instanceId)
{
    lock_guard<mutex> lock(command_mtx_);

    string sessionKey = "chat:session:" + to_string(userId);
    string nodeUsersKey = "chat:node-users:" + nodeId;

    string sessionValue = nodeId + "|" + instanceId;
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SET %s %s NX", sessionKey.c_str(), sessionValue.c_str()));
    if (reply == nullptr)
    {
        return false;
    }

    bool ok = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && string(reply->str) == "OK";
    freeReplyObject(reply);
    if (!ok)
    {
        return false;
    }

    reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SADD %s %d", nodeUsersKey.c_str(), userId));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::unbindUserNode(int userId, const string& nodeId)
{
    lock_guard<mutex> lock(command_mtx_);

    string sessionKey = "chat:session:" + to_string(userId);
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "DEL %s", sessionKey.c_str()));
    if (reply == nullptr)
    {
        return false;
    }
    freeReplyObject(reply);

    if (!nodeId.empty())
    {
        string nodeUsersKey = "chat:node-users:" + nodeId;
        reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SREM %s %d", nodeUsersKey.c_str(), userId));
        if (reply == nullptr)
        {
            return false;
        }
        freeReplyObject(reply);
    }

    return true;
}

bool Redis::getUserSession(int userId, string& nodeId, string& instanceId)
{
    lock_guard<mutex> lock(command_mtx_);

    string sessionKey = "chat:session:" + to_string(userId);
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "GET %s", sessionKey.c_str()));
    if (reply == nullptr)
    {
        return false;
    }

    bool ok = false;
    if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr)
    {
        string sessionValue = reply->str;
        size_t pos = sessionValue.rfind('|');
        if (pos == string::npos)
        {
            nodeId = sessionValue;
            instanceId.clear();
        }
        else
        {
            nodeId = sessionValue.substr(0, pos);
            instanceId = sessionValue.substr(pos + 1);
        }
        ok = !nodeId.empty();
    }

    freeReplyObject(reply);
    return ok;
}

vector<int> Redis::getNodeUsers(const string& nodeId)
{
    lock_guard<mutex> lock(command_mtx_);

    vector<int> users;
    string key = "chat:node-users:" + nodeId;
    redisReply* reply = static_cast<redisReply*>(redisCommand(command_ctx_, "SMEMBERS %s", key.c_str()));
    if (reply == nullptr)
    {
        return users;
    }

    if (reply->type == REDIS_REPLY_ARRAY)
    {
        users.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i)
        {
            if (reply->element[i] != nullptr && reply->element[i]->str != nullptr)
            {
                users.push_back(atoi(reply->element[i]->str));
            }
        }
    }

    freeReplyObject(reply);
    return users;
}
