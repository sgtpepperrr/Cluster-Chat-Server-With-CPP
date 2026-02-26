#include "db.h"
#include "config.hpp"
#include <muduo/base/Logging.h>

using namespace muduo;

MySQL::MySQL()
{
    conn_ = mysql_init(nullptr);
}

MySQL::~MySQL()
{
    if (conn_ != nullptr)
    {
        mysql_close(conn_);
    }
}

bool MySQL::connect()
{
    loadEnvFile();

    string server = getEnvOrDefault("CHAT_DB_HOST", "127.0.0.1");
    string user = getEnvOrDefault("CHAT_DB_USER", "cppuser");
    string password = getEnvOrDefault("CHAT_DB_PASSWORD", "default_pwd");
    string dbname = getEnvOrDefault("CHAT_DB_NAME", "chat");
    int port = getEnvIntOrDefault("CHAT_DB_PORT", 3306);
    string charset = getEnvOrDefault("CHAT_DB_CHARSET", "gbk");

    MYSQL* p = mysql_real_connect(conn_, server.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)
    {
        string charsetSql = "set names " + charset;
        mysql_query(conn_, charsetSql.c_str());
        LOG_INFO << __FILE__ << ":" << __LINE__ << ": " << " connect mysql success!"; 
    }
    else
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ": " << " connect mysql failed!"; 
    }

    return (p != nullptr);
}

bool MySQL::update(string sql)
{
    if (mysql_query(conn_, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ": " << sql << " 更新失败！";
        return false;
    }

    return true;
}

MYSQL_RES* MySQL::query(string sql)
{
    if (mysql_query(conn_, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ": " << sql << " 查询失败！"; 
        return nullptr;
    }

    return mysql_use_result(conn_);
}

MYSQL* MySQL::getConnection()
{
    return conn_;
}
