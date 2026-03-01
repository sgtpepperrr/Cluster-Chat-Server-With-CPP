#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <string>
#include <mutex>
#include <queue>

using namespace std;

class Redis
{
public:
    Redis();
    ~Redis();

    bool connect();
    bool publish(int channel, string message);
    bool subscribe(int channel);
    bool unsubscribe(int channel);
    
    // 在独立线程中接收订阅通道的消息
    void observe_channel_message();

    void init_notify_handler(function<void(int, string)> fn);

private:
    redisContext* publish_ctx_;
    redisContext* subscribe_ctx_;
    function<void(int, string)> notify_message_handler_;

    mutex publish_mtx_;

    mutex cmd_queue_mtx_;
    queue<string> cmd_queue_;
};

#endif /* REDIS_H */
