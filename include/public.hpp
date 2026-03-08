#ifndef PUBLIC_H
#define PUBLIC_H

enum EnMsgType
{
    LOGIN_MSG = 1,
    LOGIN_MSG_ACK,
    LOGOUT_MSG,
    REG_MSG,
    REG_MSG_ACK,
    ONE_CHAT_MSG,    // 一对一聊天消息
    ADD_FRIEND_MSG,

    CREATE_GROUP_MSG,
    ADD_GROUP_MSG,     // 加入群组
    GROUP_CHAT_MSG,    // 群聊天

    NODE_ROUTE_ONE_CHAT_MSG = 100,
    NODE_ROUTE_GROUP_CHAT_MSG,
    NODE_PING_MSG,
    NODE_PONG_MSG,
};

#endif /* PUBLIC_H */
