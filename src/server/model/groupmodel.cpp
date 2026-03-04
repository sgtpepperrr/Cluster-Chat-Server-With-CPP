#include "groupmodel.hpp"
#include "connectionpool.h"

// class GroupModel
bool GroupModel::createGroup(Group& group)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
             group.getName().c_str(), group.getDesc().c_str());

    auto conn = ConnectionPool::instance()->getConnection();
    if (conn->update(sql))
    {
        group.setId(mysql_insert_id(conn->getConnection()));
        return true;
    }

    return false;
}

void GroupModel::addGroup(int userid, int groupid, string role)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "insert into groupuser values(%d, %d, '%s')", groupid, userid, role.c_str());

    auto conn = ConnectionPool::instance()->getConnection();
    conn->update(sql);
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    // 1. 先根据userid，在groupuser表中查询出该用户所属的群组信息；
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "select a.id, a.groupname, a.groupdesc from allgroup a join groupuser g on a.id = g.groupid where g.userid = %d", userid);

    vector<Group> groupVec;
    auto conn = ConnectionPool::instance()->getConnection();
    MYSQL_RES* res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        while (row != nullptr)
        {
            Group group;
            group.setId(atoi(row[0]));
            group.setName(row[1]);
            group.setDesc(row[2]);
            groupVec.push_back(group);

            row = mysql_fetch_row(res);
        }

        mysql_free_result(res);
    }

    // 2. 再根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表联合查询，查出用户的详细信息
    for (auto& group : groupVec)
    {
        snprintf(sql, sizeof(sql), "select u.id, u.name, u.state, g.grouprole from user u join groupuser g on g.userid = u.id where g.groupid = %d", group.getId());

        MYSQL_RES* res = conn->query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            while (row != nullptr)
            {
                GroupUser groupUser;
                groupUser.setId(atoi(row[0]));
                groupUser.setName(row[1]);
                groupUser.setState(row[2]);
                groupUser.setRole(row[3]);
                group.getUsers().push_back(groupUser);

                row = mysql_fetch_row(res);
            }

            mysql_free_result(res);
        }
    }

    return groupVec;
}

// 根据指定的groupid查询并返回群组用户id列表（不包括userid自己）
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql), "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    auto conn = ConnectionPool::instance()->getConnection();
    MYSQL_RES* res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        while (row != nullptr)
        {
            idVec.push_back(atoi(row[0]));
            row = mysql_fetch_row(res);
        }

        mysql_free_result(res);
    }

    return idVec;
}
