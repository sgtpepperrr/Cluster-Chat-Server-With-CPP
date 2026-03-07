#include "chatserver.hpp"
#include "chatservice.hpp"
#include "config.hpp"
#include "json.hpp"
#include <muduo/base/Logging.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

namespace
{
using steady_clock = std::chrono::steady_clock;
using microseconds = std::chrono::microseconds;

uint64_t nowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

double avg(uint64_t total, uint64_t count)
{
    if (count == 0)
    {
        return 0.0;
    }
    return static_cast<double>(total) / static_cast<double>(count);
}

double pct(uint64_t part, uint64_t total)
{
    if (total == 0)
    {
        return 0.0;
    }
    return static_cast<double>(part) * 100.0 / static_cast<double>(total);
}

bool perfTraceEnabled()
{
    static bool enabled = []() {
        loadEnvFile();
        return getEnvIntOrDefault("CHAT_PERF_TRACE", 0) > 0;
    }();
    return enabled;
}

uint64_t perfReportEvery()
{
    static uint64_t every = []() {
        int raw = getEnvIntOrDefault("CHAT_PERF_REPORT_EVERY", 2000);
        if (raw <= 0)
        {
            raw = 2000;
        }
        return static_cast<uint64_t>(raw);
    }();
    return every;
}

std::atomic<uint64_t> g_onMessageFrameCount(0);
std::atomic<uint64_t> g_onMessageParseUs(0);
std::atomic<uint64_t> g_onMessageParseErrorCount(0);
std::atomic<uint64_t> g_onMessageDispatchCount(0);
std::atomic<uint64_t> g_onMessageDispatchUs(0);

void reportOnMessagePerf(uint64_t frameCount)
{
    uint64_t parseErrors = g_onMessageParseErrorCount.load(std::memory_order_relaxed);
    uint64_t dispatchCount = g_onMessageDispatchCount.load(std::memory_order_relaxed);

    LOG_INFO << "[perf][onMessage] frames=" << frameCount
             << " parse_error_rate=" << pct(parseErrors, frameCount) << "%"
             << " avg_parse_us="
             << avg(g_onMessageParseUs.load(std::memory_order_relaxed), frameCount)
             << " avg_dispatch_us="
             << avg(g_onMessageDispatchUs.load(std::memory_order_relaxed), dispatchCount)
             << " dispatch_count=" << dispatchCount;
}
} // namespace

ChatServer::ChatServer(EventLoop* loop,
               const InetAddress& listenAddr,  // IP + port
               const string& nameArg)          // 服务器的名字
    : server_(loop, listenAddr, nameArg)
    , loop_(loop)
{
    // 给服务器注册用户连接的创建和断开的回调
    server_.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 给服务器注册用户读写事件的回调
    server_.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置服务器端的线程数量
    server_.setThreadNum(4);
}

void ChatServer::start()
{
    server_.start();
}

// 处理用户连接的创建和断开
void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 处理用户的读写事件
void ChatServer::onMessage(const TcpConnectionPtr& conn,
               Buffer* buffer,
               Timestamp time)  // 接收到数据的时间信息
{
    bool perf = perfTraceEnabled();
    string buf = buffer->retrieveAllAsString();

    // 按 \0 分割处理每条消息（处理 TCP 粘包）
    size_t start = 0;
    while (start < buf.size())
    {
        size_t end = buf.find('\0', start);
        if (end == string::npos)
        {
            end = buf.size();
        }

        if (end > start)
        {
            uint64_t parseStartUs = 0;
            if (perf)
            {
                parseStartUs = nowUs();
            }
            try
            {
                json js = json::parse(buf.begin() + start, buf.begin() + end);
                uint64_t parsedUs = 0;
                if (perf)
                {
                    parsedUs = nowUs();
                    g_onMessageParseUs.fetch_add(parsedUs - parseStartUs, std::memory_order_relaxed);
                }

                auto msgHandler = ChatService::instance()->getHandler(js["msgId"].get<int>());
                msgHandler(conn, js, time);

                if (perf)
                {
                    g_onMessageDispatchCount.fetch_add(1, std::memory_order_relaxed);
                    g_onMessageDispatchUs.fetch_add(nowUs() - parsedUs, std::memory_order_relaxed);

                    uint64_t frameCount = g_onMessageFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (frameCount % perfReportEvery() == 0)
                    {
                        reportOnMessagePerf(frameCount);
                    }
                }
            }
            catch (const json::exception& e)
            {
                if (perf)
                {
                    g_onMessageParseUs.fetch_add(nowUs() - parseStartUs, std::memory_order_relaxed);
                    g_onMessageParseErrorCount.fetch_add(1, std::memory_order_relaxed);

                    uint64_t frameCount = g_onMessageFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (frameCount % perfReportEvery() == 0)
                    {
                        reportOnMessagePerf(frameCount);
                    }
                }
                LOG_ERROR << "onMessage parse error: " << e.what();
            }
        }

        start = end + 1;
    }
}
