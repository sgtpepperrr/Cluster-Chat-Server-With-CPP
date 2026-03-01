#include "redis.hpp"
#include "config.hpp"
#include <iostream>
#include <poll.h>

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
    lock_guard<mutex> lock(publish_mtx_);

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
