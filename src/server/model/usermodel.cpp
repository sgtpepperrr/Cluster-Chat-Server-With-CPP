#include "usermodel.hpp"
#include "connectionpool.h"
#include <iostream>

using namespace std;

bool UserModel::insert(User& user)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "insert into user(name, password, state) values('%s', '%s', '%s')",
             user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    auto conn = ConnectionPool::instance()->getConnection();
    if (conn->update(sql))
    {
        user.setId(mysql_insert_id(conn->getConnection()));
        return true;
    }

    return false;
}

User UserModel::query(int id)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "select * from user where id = %d", id);

    auto conn = ConnectionPool::instance()->getConnection();
    MYSQL_RES* res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setPwd(row[2]);
            user.setState(row[3]);

            mysql_free_result(res);

            return user;
        }
    }

    return User();
}

bool UserModel::updateState(User user)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "update user set state = '%s' where id = '%d'",
             user.getState().c_str(), user.getId());

    auto conn = ConnectionPool::instance()->getConnection();
    return conn->update(sql);
}

void UserModel::resetState()
{
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    auto conn = ConnectionPool::instance()->getConnection();
    conn->update(sql);
}
