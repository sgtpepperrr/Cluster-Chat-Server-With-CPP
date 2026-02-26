#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"

// 维护群组信息的操作接口方法
class GroupModel
{
public:
    bool createGroup(Group& group);
    void addGroup(int userid, int groupid, string role);

    // 查询用户所在群组信息
    vector<Group> queryGroups(int userid);

    // 根据指定的groupid查询并返回群组用户id列表（不包括userid自己）
    vector<int> queryGroupUsers(int userid, int groupid);
};

#endif /* GROUPMODEL_H */
