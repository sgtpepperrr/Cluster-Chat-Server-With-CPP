#include "redis.hpp"
#include "config.hpp"
#include <iostream>

using namespace std;

Redis::Redis()
    : publish_ctx_(nullptr)
    , subscribe_ctx_(nullptr)
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

    // 在单独的线程中，监听通道上的事件，给业务层进行上报消息
    thread t([&]() { observe_channel_message(); });
    t.detach();

    cout << "connect redis-server success!" << endl;
    return true;
}

bool Redis::publish(int channel, string message)
{
    redisReply* reply = (redisReply*)redisCommand(publish_ctx_, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::subscribe(int channel)
{
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observe_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源
    if (REDIS_ERR == redisAppendCommand(this->subscribe_ctx_, "SUBSCRIBE %d", channel))
    {
        cerr << "subscribe command failed!" << endl;
        return false;
    }

    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->subscribe_ctx_, &done))
        {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    // redisGetReply

    return true;
}

bool Redis::unsubscribe(int channel)
{
    if (REDIS_ERR == redisAppendCommand(this->subscribe_ctx_, "UNSUBSCRIBE %d", channel))
    {
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }

    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->subscribe_ctx_, &done))
        {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }

    return true;
}

// 在独立线程中接收订阅通道中的消息
void Redis::observe_channel_message()
{
    redisReply* reply = nullptr;
    while (REDIS_OK == redisGetReply(this->subscribe_ctx_, (void **)&reply))
    {
        // 订阅收到的消息是一个带三元素的数组
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            // 给业务层上报通道上发生的消息
            notify_message_handler_(atoi(reply->element[1]->str) , reply->element[2]->str);
        }

        freeReplyObject(reply);
    }

    cerr << ">>>>>>>>>>>>> observe_channel_message quit <<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int,string)> fn)
{
    this->notify_message_handler_ = fn;
}
