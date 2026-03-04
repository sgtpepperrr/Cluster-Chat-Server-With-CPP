# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-01 17:08:48 CST
- Generated at (UTC): 2026-03-01 09:08:48 UTC
- Branch: `main`
- Commit: `94e8bc1bc7edaf9c215881e8d5bd6a6b055dbba4`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir /home/Jade/chat-server/github/Cluster-Chat-Server-With-CPP/bench/runs/baseline-2026-03-01`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..200` (`200` users)
- Messages per sender: `200`
- Send interval: `1.0 ms`
- Group ID: `1 (21 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 200 | 200 | 22.3/s | 4604.1 | 8407.6 | 8727.4 | 50.0% |
| One-to-One Chat | 100 | 20000 | 452.6/s | 0.3 | 2850.7 | 7743.9 | 94.2% |
| Group Chat (1->20) | 21 | 200 | 0.0/s | 0.0 | 0.0 | 0.0 | 2000.0% |

## 4. Environment Metadata
- Metadata file: `/home/Jade/chat-server/github/Cluster-Chat-Server-With-CPP/bench/runs/baseline-2026-03-01/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh /home/Jade/chat-server/github/Cluster-Chat-Server-With-CPP/bench/runs/baseline-2026-03-01/meta.md`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:
Logs:
```
20260301 06:44:08.295849Z 3134172 INFO  /home/Jade/chat-server/github/Cluster-Chat-Server-With-CPP/src/server/db/db.cpp:36:  connect mysql success! - db.cpp:36
20260301 06:44:08.296775Z 3134160 INFO  TcpServer::removeConnectionInLoop [ChatServer] - connection ChatServer-127.0.0.1:6000#120 - TcpServer.cc:109
20260301 06:44:08.301280Z 3134170 INFO  TcpServer::removeConnectionInLoop [ChatServer] - connection ChatServer-127.0.0.1:6002#170 - TcpServer.cc:109
free(): invalid pointer
free(): invalid pointer

20260301 06:43:26.997169Z 3134162 ERROR TcpConnection::handleError [ChatServer-127.0.0.1:6000#94] - SO_ERROR = 32 Broken pipe - TcpConnection.cc:426



[1]-  Aborted                 (core dumped) ./bin/ChatServer 127.0.0.1 6000
[2]+  Segmentation fault      (core dumped) ./bin/ChatServer 127.0.0.1 6002
```

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
1. 保证Redis 线程安全
publish_ctx_和subscribe_ctx_都有可能被多线程访问，目前都没有加保护。
2. onMessage 异常处理 + 粘包处理
收到的消息可能存在TCP粘包的问题，之前接收方没有\0作为数据边界，发送方却用了，导致并发量大的时候可能会有这个问题。
用 \0 分割 buffer 中可能存在的多条消息，每条单独 json::parse，外套 try-catch。这样既处理了粘包问题，也防止解析异常崩溃。