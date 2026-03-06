#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"
#include <unordered_map>
#include <vector>

// User表的数据操作类
class UserModel
{
public:
    bool insert(User& user);
    User query(int id);
    unordered_map<int, string> queryStates(const vector<int>& ids);
    bool updateState(User user);
    void resetState();
};

#endif /* USERMODEL_H */
