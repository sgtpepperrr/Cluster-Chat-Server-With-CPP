#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include "db.h"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

class ConnectionPool
{
public:
    // 获取单例
    static ConnectionPool* instance();

    // 从池中借一个连接。
    // 返回的 shared_ptr 析构时，连接自动归还到池中，而不是被 delete。
    // 若池中暂无空闲连接，调用者会阻塞等待。
    shared_ptr<MySQL> getConnection();

private:
    ConnectionPool();

    int maxSize_;           // 池的大小（连接总数）
    queue<MySQL*> queue_;   // 空闲连接队列
    mutex mutex_;
    condition_variable cv_;
};

#endif /* CONNECTIONPOOL_H */
