# Cluster Chat Server (C++)

基于 `Muduo + MySQL + Redis + Nginx(stream) + CMake` 的集群聊天系统。  
支持 TCP 负载均衡、多节点部署、会话归属路由、节点间直连转发和离线消息存储。

## 仓库描述（GitHub Description）
`A C++ cluster chat server using Muduo, Nginx TCP load balancing, Redis for control-plane presence, node-to-node TCP routing, and MySQL for private chat, group chat, and offline messages.`

## 核心特性
- Nginx `stream` 层 TCP 负载均衡（客户端统一连负载均衡入口）
- 多 ChatServer 节点水平扩展
- Redis 控制面维护节点注册、心跳和 `userId -> nodeId` 会话归属
- ChatServer 节点间专用 TCP 通道做在线消息定向投递
- MySQL 持久化用户、好友、群组、离线消息
- 用户注册 / 登录 / 退出
- 一对一聊天、好友管理、群聊
- 客户端收发分离：主线程发送，子线程接收（信号量同步 ACK）

## 架构概览
```text
ChatClient
   |
   v
Nginx (TCP Load Balancer, stream)
   |
   +--> ChatServer-1 <---------+
   |      |                    |
   |      +--> Redis (控制面)   |
   |      +--> MySQL           |
   |
   +--> ChatServer-2 <---------+
   |      |
   |      +--> Redis (控制面)
   |      +--> MySQL
   |
   +--> ChatServer-N

节点间在线消息：ChatServer <---- TCP ----> ChatServer
```

## 目录结构
```text
.
├── .env.example
├── build.sh
├── docs/
│   ├── schema.sql
│   └── nginx-tcp-lb.conf.example
├── include/
├── src/
│   ├── server/
│   └── client/
├── thirdparty/
├── build/
└── bin/
```

## 环境依赖
Ubuntu 示例：
- `g++`
- `cmake`
- `libmysqlclient-dev`
- `mysql-server`
- `redis-server`
- `libhiredis-dev`
- `libmuduo-dev`（或源码安装 Muduo）
- `nginx`（需启用 `stream` 模块）

## 快速开始
1. 初始化数据库
```bash
mysql -u root -p < docs/schema.sql
```

2. 配置环境变量
```bash
cp .env.example .env
```
然后按你的环境修改 `.env`（MySQL/Redis 地址、账号、密码，以及节点间路由端口）。

3. 编译
```bash
./build.sh
```

4. 启动多个服务节点（示例）
```bash
# 节点 1
CHAT_INTER_NODE_PORT=6100 ./bin/ChatServer 127.0.0.1 6000

# 节点 2
CHAT_INTER_NODE_PORT=6101 ./bin/ChatServer 127.0.0.1 6001
```

说明：
- 客户端入口端口仍然是命令行里的 `6000/6001`
- 节点间内部通信端口由 `CHAT_INTER_NODE_PORT` 指定
- `ClusterRouter` 使用 Muduo 异步客户端长连接，I/O 线程数由 `CHAT_INTER_NODE_IO_THREADS` 指定
- 连接建立阶段的瞬时排队上限由 `CHAT_INTER_NODE_CONNECTING_QUEUE_LIMIT` 指定
- 单连接输出缓冲背压阈值由 `CHAT_INTER_NODE_HIGH_WATER_MARK` 指定
- 若未显式设置，内部端口默认使用 `客户端端口 + 100`

5. 配置并启动 Nginx TCP 负载均衡
- 参考 `docs/nginx-tcp-lb.conf.example`
- 示例中对外监听端口是 `7000`

6. 客户端连接负载均衡入口
```bash
./bin/ChatClient 127.0.0.1 7000
```

## 客户端命令
- `help`
- `chat:friendId:message`
- `addfriend:friendId`
- `creategroup:groupname:groupdesc`
- `addgroup:groupId`
- `groupchat:groupId:message`
- `logout`
