# Cluster Chat Server 基线压测指南

本工具用于对集群聊天服务器做基线性能测试，覆盖三个核心场景：登录吞吐、单聊吞吐/延迟、群聊扇出。

纯 Python 标准库实现，无需安装第三方依赖。

## 快速流程（推荐先看）

如果你只想知道“跑完 `run.py` 之后做什么”，先看：

`bench/WORKFLOW.md`

该文件定义了固定目录规则：

`bench/runs/<run-id>/`

现在支持自动生成报告（不需要手动抄结果）：

```bash
./scripts/new_bench_run.sh baseline-2026-03-01
python3 bench/run.py all --port 8000 --start-id 100 --count 200 --group-id 5 --messages 200 --run-dir bench/runs/baseline-2026-03-01
./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-01/meta.md
```

## 前置条件

- Python 3.7+
- ChatServer 节点已启动（可选配 Nginx 负载均衡）
- 测试用户已通过 `scripts/generate_bench_sql.sh` 导入数据库
- MySQL、Redis 正常运行

## 第一步：查出测试数据 ID

```bash
mysql -u cppuser -p -e "SELECT MIN(id), MAX(id), COUNT(*) FROM chat.user WHERE name LIKE 'bench_%'"
mysql -u cppuser -p -e "SELECT id FROM chat.allgroup WHERE groupname = 'benchgroup'"
```

- `MIN(id)` 即为后续命令的 `--start-id`
- group 查询结果即为 `--group-id`

## 第二步：重置测试状态（每次跑测试前必须执行）

```bash
mysql -u cppuser -p -e "UPDATE chat.user SET state='offline' WHERE name LIKE 'bench_%'"
mysql -u cppuser -p -e "DELETE FROM chat.offlinemessage WHERE userid IN \
      (SELECT id FROM chat.user WHERE name LIKE 'bench_%')"
```

上次测试残留的 `online` 状态会导致登录失败（服务端拒绝重复登录），所以每次测试前必须重置。

## 第三步：确保服务端运行

```bash
./bin/ChatServer 127.0.0.1 6000 &
./bin/ChatServer 127.0.0.1 6002 &
# Nginx 在 8000 端口做 TCP 负载均衡
```

## 第四步：运行测试

以下示例假设 `--start-id 100`、`--group-id 5`，请替换为你实际查到的值。

### 单独运行某个场景

```bash
# 场景 1：登录吞吐（200 并发）
python3 bench/run.py login --port 8000 --start-id 100 --count 200

# 场景 2：单聊吞吐/延迟（100 对，每对 200 条消息）
python3 bench/run.py chat --port 8000 --start-id 100 --count 200 --messages 200

# 场景 3：群聊扇出（1 发 20 收，200 条消息）
python3 bench/run.py group --port 8000 --start-id 100 --group-id 5 --messages 200
```

### 一键运行全部三个场景

```bash
python3 bench/run.py all --port 8000 --start-id 100 --count 200 --group-id 5 --messages 200
```

`all` 模式会依次执行 login → chat → group，场景间自动暂停 3 秒，最后输出 Markdown 汇总表。

### 对比直连 vs Nginx（可选）

```bash
# 直连单节点
python3 bench/run.py login --port 6000 --start-id 100 --count 200

# 重置状态后，走 Nginx 负载均衡
python3 bench/run.py login --port 8000 --start-id 100 --count 200
```

通过对比两组数据可以看出 Nginx 引入的额外开销。

## 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `scenario` | 必填 | `login` / `chat` / `group` / `all` |
| `--host` | `127.0.0.1` | 服务端地址 |
| `--port` | `8000` | 连接端口（Nginx 或直连） |
| `--start-id` | 必填 | 第一个测试用户的 ID |
| `--count` | `200` | 使用的测试用户数量 |
| `--password` | `bench123` | 测试用户密码 |
| `--messages` | `100` | chat/group 场景下每个 sender 发送的消息数 |
| `--group-id` | — | group/all 场景必填，测试群的 ID |
| `--group-members` | `21` | 测试群的成员数（含 sender） |
| `--interval` | `0.001` | 每个 sender 两次发送之间的间隔（秒） |

### 关于 `--interval`

默认 1ms（即每个 sender 最高 1000 msg/s）。

服务端 `onMessage` 的实现是 `buffer->retrieveAllAsString()` 取全部数据后调用 `json::parse()`，一次只能解析一条 JSON 消息。如果两条消息在 TCP 层粘包合并到同一次读取中，服务端会解析失败。1ms 间隔在正常负载下足够避免此问题。

如需测试极限吞吐，可设 `--interval 0`，但预期会看到错误率上升——这本身也是一个有价值的测试发现（暴露了协议层的粘包问题）。

## 输出说明

每个场景输出一份详细报告：

```
====================================================
  Login Throughput
====================================================
  Concurrency:      200
  Total requests:   200
  Duration:         2.34 s
  Throughput:       85.5 req/s
  Latency min:      8.12 ms
  Latency P50:      18.30 ms
  Latency P95:      45.20 ms
  Latency P99:      67.80 ms
  Latency max:      123.45 ms
  Successes:        196
  Errors:           4
  Disconnects:      0
  Error rate:       2.0%
====================================================
```

群聊场景额外输出 fan-out 指标（一条消息从发出到所有接收者都收到的延迟）。

`all` 模式最后会输出 Markdown 汇总表，可直接粘贴到 baseline 文档中：

```
## Baseline Summary

| Scenario | Concurrency | Requests | Throughput |  P50  |  P95  |  P99  | Errors |
|----------|-------------|----------|------------|-------|-------|-------|--------|
| Login Throughput       |         200 |      200 |     85.5/s |  18.3 |  45.2 |  67.8 |   2.0% |
| One-to-One Chat        |         100 |    10000 |   2341.0/s |   3.2 |  12.1 |  25.4 |   0.1% |
| Group Chat (1->20)     |          21 |      200 |     89.3/s |   5.1 |  18.3 |  35.7 |   0.5% |
```

## 延迟测量原理

在消息体 `msg` 字段中嵌入发送端的 `time.monotonic()` 高精度时间戳，接收端解析后与自身时间做差。由于压测客户端和服务端在同一台机器上，`monotonic` 时钟一致，无需时钟同步。

## 记录 baseline

建议将以下信息一并记录到 baseline 文档中：

- 测试日期
- 机器配置（CPU 型号/核心数、内存、磁盘类型）
- OS 版本、内核版本
- ChatServer 节点数和端口
- 是否经过 Nginx
- MySQL / Redis 配置
- `--count`、`--messages`、`--interval` 等测试参数
- 汇总表结果
