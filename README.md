# Cluster Chat Server (C++)

基于 `Muduo + MySQL + Redis + Nginx(stream) + CMake` 的集群聊天系统。  
支持 TCP 负载均衡、多节点部署、跨节点消息转发和离线消息存储。

## 仓库描述（GitHub Description）
`A C++ cluster chat server using Muduo, Nginx TCP load balancing, Redis Pub/Sub, and MySQL for private chat, group chat, cross-node routing, and offline messages.`

## 核心特性
- Nginx `stream` 层 TCP 负载均衡（客户端统一连负载均衡入口）
- 多 ChatServer 节点水平扩展
- Redis Pub/Sub 做跨节点消息路由
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
   +--> ChatServer-1 ----+
   |                     |
   +--> ChatServer-2 ----+--> Redis (Pub/Sub)
   |                     |
   +--> ChatServer-N ----+
             |
             +--> MySQL (业务数据存储)
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
然后按你的环境修改 `.env`（MySQL/Redis 地址、账号、密码）。

3. 编译
```bash
./build.sh
```

4. 启动多个服务节点（示例）
```bash
./bin/ChatServer 127.0.0.1 6000
./bin/ChatServer 127.0.0.1 6001
```

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
