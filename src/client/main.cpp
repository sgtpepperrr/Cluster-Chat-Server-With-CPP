#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>
#include <cctype>
#include <atomic>

using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 当前系统登录的用户信息
User g_currentUser;

// 当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;

// 当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

bool g_isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwSem;

atomic_bool g_isLoginSuccess{false};

// 显示当前登录用户的基本信息
void showCurrentUserData();

void readTaskHandler(int clientfd);

string getCurrentTime();

void mainMenu(int clientfd);

// 聊天客户端
// main线程用作发送线程，子线程用作接收线程
int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 8000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char* ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create failed" << endl;
        exit(-1);
    }

    // 添加需要连接的server信息
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // 连接server
    if (-1 == connect(clientfd, (sockaddr*)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server failed" << endl;
        close(clientfd);
        exit(-1);
    }

    sem_init(&rwSem, 0, 0);

    // 启动接收线程负责接收数据
    std::thread readTask(readTaskHandler, clientfd);
    readTask.detach();

    // main线程作发送线程
    for (;;)
    {
        cout << "====================================================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "====================================================" << endl;
        cout << "choice: ";
        
        int choice = 0;
        cin >> choice;
        cin.get();  // 读掉缓冲区残留的回车符

        switch (choice)
        {
        case 1:  // login
        {
            int id = 0;
            char pwd[50] = { 0 };
            cout << "userid: ";
            cin >> id;
            cin.get();  // 读掉缓冲区残留的回车符
            cout << "password: ";
            cin.getline(pwd, 50);

            json js;
            js["msgId"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), request.length() + 1, 0);
            if (-1 == len)
            {
                cerr << "send login msg error: " << request << endl;
            }

            sem_wait(&rwSem);
           
            if (g_isLoginSuccess)
            {
                g_isMainMenuRunning = true;
                mainMenu(clientfd);
            }

            break;
        }

        case 2:  // register
        {
            char name[50] = { 0 };
            char pwd[50] = { 0 };
            cout << "username: ";
            cin.getline(name, 50);
            cout << "password: ";
            cin.getline(pwd, 50);

            json js;
            js["msgId"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), request.length() + 1, 0);
            if (-1 == len)
            {
                cerr << "send reg msg error: " << request << endl;
            }

            sem_wait(&rwSem);

            break;
        }
            
        case 3:  // quit
            close(clientfd);
            sem_destroy(&rwSem);
            exit(0);

        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

void doLoginResponse(json& respJs)
{
    if (0 != respJs["errno"].get<int>())
    {
        cerr << respJs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else
    {
        // 登录成功
        g_isLoginSuccess = true;
        g_currentUser.setId(respJs["id"].get<int>());
        g_currentUser.setName(respJs["name"]);

        // 好友列表
        if (respJs.contains("friends"))
        {
            g_currentUserFriendList.clear();

            vector<string> vec = respJs["friends"];
            for (string& str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 群组列表
        if (respJs.contains("groups"))
        {
            g_currentUserGroupList.clear();

            vector<string> vec1 = respJs["groups"];
            for (string& str : vec1)
            {
                json js = json::parse(str);
                Group group;
                group.setId(js["id"].get<int>());
                group.setName(js["groupname"]);
                group.setDesc(js["groupdesc"]);

                vector<string> vec2 = js["users"];
                for (string& usr : vec2)
                {
                    GroupUser user;
                    json usrJs = json::parse(usr);
                    user.setId(usrJs["id"].get<int>());
                    user.setName(usrJs["name"]);
                    user.setState(usrJs["state"]);
                    user.setRole(usrJs["role"]);
                    group.getUsers().push_back(user);
                }

                g_currentUserGroupList.push_back(group);
            }
        }

        showCurrentUserData();

        // 显示当前用户的离线消息
        if (respJs.contains("offlinemsg"))
        {
            vector<string> vec = respJs["offlinemsg"];
            for (string& str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + " said: " + xxx
                if (ONE_CHAT_MSG == js["msgId"].get<int>())
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                        << " said: " << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "Group[" << js["groupid"] << "] message: "
                        << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                        << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }
    }
}

void doRegResponse(json& respJs)
{
    if (0 != respJs["errno"].get<int>())
    {
        cerr << " Username is already exist, register failed!" << endl;
    }
    else
    {
        cout << "register success, userid is " << respJs["id"] 
                << ". Please use userid to login." << endl;
    }
}

// 查找第一个完整 JSON 对象的结束位置
// 返回结束位置（闭合 } 的下一个字符），未找到返回 string::npos
static size_t findJsonEnd(const string& buf, size_t start)
{
    int depth = 0;
    bool inStr = false;
    bool esc = false;

    for (size_t i = start; i < buf.size(); ++i)
    {
        char c = buf[i];

        if (esc)
        {
            esc = false;
            continue;
        }
        if (inStr)
        {
            if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }

        if (c == '"') { inStr = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}')
        {
            if (--depth == 0) return i + 1;
        }
    }

    return string::npos;  // 不完整，还需要更多数据
}

void readTaskHandler(int clientfd)
{
    string recvBuf;
    char tmpBuf[4096];

    for (;;)
    {
        int len = recv(clientfd, tmpBuf, sizeof(tmpBuf), 0);
        if (len <= 0)
        {
            close(clientfd);
            exit(-1);
        }

        recvBuf.append(tmpBuf, len);

        // 从缓冲区中提取并处理所有完整的 JSON 消息
        size_t pos = 0;
        while (pos < recvBuf.size())
        {
            // 跳过空白和 \0
            while (pos < recvBuf.size() &&
                   (recvBuf[pos] == '\0' || recvBuf[pos] == ' ' ||
                    recvBuf[pos] == '\n' || recvBuf[pos] == '\r' ||
                    recvBuf[pos] == '\t'))
            {
                ++pos;
            }
            if (pos >= recvBuf.size()) break;

            size_t end = findJsonEnd(recvBuf, pos);
            if (end == string::npos) break;  // 不完整，等下次 recv

            try
            {
                json js = json::parse(recvBuf.begin() + pos, recvBuf.begin() + end);
                int msgType = js["msgId"].get<int>();

                if (ONE_CHAT_MSG == msgType)
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                         << " said: " << js["msg"].get<string>() << endl;
                }
                else if (GROUP_CHAT_MSG == msgType)
                {
                    cout << "Group[" << js["groupid"] << "] message: "
                        << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                         << " said: " << js["msg"].get<string>() << endl;
                }
                else if (LOGIN_MSG_ACK == msgType)
                {
                    doLoginResponse(js);
                    sem_post(&rwSem);
                }
                else if (REG_MSG_ACK == msgType)
                {
                    doRegResponse(js);
                    sem_post(&rwSem);
                }
            }
            catch (const json::exception& e)
            {
                cerr << "JSON parse error: " << e.what() << endl;
            }

            pos = end;
        }

        // 移除已处理的数据，保留不完整的尾部
        if (pos > 0)
        {
            recvBuf.erase(0, pos);
        }
    }
}

void help(int fd = 0, string str = "");
void chat(int, string);
void addfriend(int, string);
void creategroup(int, string);
void addgroup(int, string);
void groupchat(int, string);
void logout(int, string);

static string trimCopy(const string& s)
{
    size_t begin = 0;
    while (begin < s.size() && isspace(static_cast<unsigned char>(s[begin])))
    {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin && isspace(static_cast<unsigned char>(s[end - 1])))
    {
        --end;
    }

    return s.substr(begin, end - begin);
}

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = 
{
    {"help", "显示所有支持的命令，格式 help"},
    {"chat", "一对一聊天，格式 chat:friendId:message"},
    {"addfriend", "添加好友，格式 addfriend:friendId"},
    {"creategroup", "创建群组，格式 creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式 addgroup:groupId"},
    {"groupchat", "群聊，格式 groupchat:groupId:message"},
    {"logout", "退出登录，格式 logout"},
};

// 注册系统支持的客户端命令
unordered_map<string, function<void(int, string)>> commandHandlerMap = 
{
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"logout", logout}
};

void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = { 0 };
    while (g_isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandBuf(buffer);
        size_t sepPos = commandBuf.find(':');
        size_t sepLen = 1;
        if (sepPos == string::npos)
        {
            sepPos = commandBuf.find("：");
            if (sepPos != string::npos)
            {
                sepLen = string("：").size();
            }
        }

        string command = (sepPos == string::npos) ? commandBuf : commandBuf.substr(0, sepPos);
        command = trimCopy(command);
        if (command.empty())
        {
            continue;
        }

        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调
        string args;
        if (sepPos != string::npos)
        {
            args = commandBuf.substr(sepPos + sepLen);
        }
        it->second(clientfd, args);
    }
}

void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto& command : commandMap)
    {
        cout << command.first << " : " << command.second << endl;
    }
    cout << endl;
}

void addfriend(int clientfd, string str)
{
    int friendId = stoi(str);
    json js;
    js["msgId"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendId"] = friendId;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void chat(int clientfd, string str)
{
    // str = friendId:message
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendId = stoi(str.substr(0, idx));
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgId"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["to"] = friendId;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

void creategroup(int clientfd, string str)
{
    // groupname:groupdesc
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgId"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}

void addgroup(int clientfd, string str)
{
    // groupId
    int groupId = stoi(str);
    json js;
    js["msgId"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupId;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void groupchat(int clientfd, string str)
{
    // groupId:message
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupId = stoi(str.substr(0, idx));
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgId"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupId;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}

void logout(int clientfd, string str)
{
    json js;
    js["msgId"] = LOGOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        cerr << "send logout msg error -> " << buffer << endl;
    }
    else
    {
        g_isMainMenuRunning = false;
    }
}

// 显示当前登录用户的基本信息
void showCurrentUserData()
{
    cout << "==================== login user ====================" << endl;
    cout << "current login user => id: " << g_currentUser.getId() << " name: " << g_currentUser.getName() << endl;

    cout << "-------------------- friend list --------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User& user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }

    cout << "-------------------- group list --------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group& group : g_currentUserGroupList)
        {
            cout << "[" <<group.getId() << "] " << group.getName() << ": " << group.getDesc() << endl;
            cout << "Group members:" << endl;
            for (GroupUser& user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
            cout << endl;
        }
    }

    cout << "====================================================" << endl;
}

string getCurrentTime()
{
    auto tt = chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm* ptm = localtime(&tt);
    char date[60] = { 0 };
    snprintf(date, sizeof(date), "%d-%02d-%02d %02d:%02d:%02d",
             (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
             (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return string(date);
}
