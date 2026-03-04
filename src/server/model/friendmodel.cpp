#include "friendmodel.hpp"
#include "connectionpool.h"

void FriendModel::insert(int userId, int friendId)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "insert into friend values(%d, %d)", userId, friendId);

    auto conn = ConnectionPool::instance()->getConnection();
    conn->update(sql);
}

// 返回用户的好友列表
vector<User> FriendModel::query(int userId)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "select u.id, u.name, u.state from friend f join user u on f.friendid = u.id where f.userid = %d", userId);

    vector<User> vec;
    auto conn = ConnectionPool::instance()->getConnection();
    MYSQL_RES* res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        while (row != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setState(row[2]);
            vec.push_back(user);

            row = mysql_fetch_row(res);
        }

        mysql_free_result(res);
    }

    return vec;
}
