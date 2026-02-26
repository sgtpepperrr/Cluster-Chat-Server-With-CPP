#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>

using namespace std;

// 维护好友信息的操作接口方法
class FriendModel
{
public:
    void insert(int userId, int friendId);

    // 返回用户的好友列表
    vector<User> query(int userId);
};

#endif /* FRIENDMODEL_H */
