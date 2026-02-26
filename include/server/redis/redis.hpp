#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <string>

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
};

#endif /* REDIS_H */
