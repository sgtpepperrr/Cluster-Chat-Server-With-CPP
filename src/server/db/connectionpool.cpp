#include "connectionpool.h"
#include "config.hpp"
#include <muduo/base/Logging.h>

using namespace muduo;

ConnectionPool* ConnectionPool::instance()
{
    static ConnectionPool pool;
    return &pool;
}

ConnectionPool::ConnectionPool()
{
    loadEnvFile();
    maxSize_ = getEnvIntOrDefault("CHAT_DB_POOL_SIZE", 20);

    for (int i = 0; i < maxSize_; i++)
    {
        MySQL* conn = new MySQL();
        if (conn->connect())
        {
            queue_.push(conn);
        }
        else
        {
            delete conn;
            LOG_ERROR << "ConnectionPool: failed to create connection " << i;
        }
    }

    LOG_INFO << "ConnectionPool: initialized with " << queue_.size() << "/" << maxSize_ << " connections";
}

shared_ptr<MySQL> ConnectionPool::getConnection()
{
    unique_lock<mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty(); });

    MySQL* conn = queue_.front();
    queue_.pop();

    return shared_ptr<MySQL>(conn, [this](MySQL* conn)
    {
        lock_guard<mutex> lock(mutex_);
        queue_.push(conn);
        cv_.notify_one();
    });
}
