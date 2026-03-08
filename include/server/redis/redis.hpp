#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <string>
#include <mutex>
#include <queue>
#include <vector>
#include "clustertypes.hpp"

using namespace std;

class Redis
{
public:
    Redis();
    ~Redis();

    bool connect();
    // Return subscriber count. <=0 means no subscriber or publish failed.
    int publish(int channel, const string& message);
    bool subscribe(int channel);
    bool unsubscribe(int channel);

    bool registerNode(const NodeMeta& nodeMeta, int ttlSeconds);
    bool unregisterNode(const string& nodeId);
    bool getNode(const string& nodeId, NodeMeta& nodeMeta);
    vector<string> getNodeIds();

    bool bindUserNode(int userId, const string& nodeId, const string& instanceId);
    bool unbindUserNode(int userId, const string& nodeId);
    bool getUserSession(int userId, string& nodeId, string& instanceId);
    vector<int> getNodeUsers(const string& nodeId);
    
    // 在独立线程中接收订阅通道的消息
    void observe_channel_message();

    void init_notify_handler(function<void(int, string)> fn);

private:
    redisContext* publish_ctx_;
    redisContext* subscribe_ctx_;
    redisContext* command_ctx_;
    function<void(int, string)> notify_message_handler_;

    mutex publish_mtx_;
    mutex command_mtx_;

    mutex cmd_queue_mtx_;
    queue<string> cmd_queue_;
};

#endif /* REDIS_H */
