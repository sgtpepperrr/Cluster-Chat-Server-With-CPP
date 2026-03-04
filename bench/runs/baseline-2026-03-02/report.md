# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-02 22:20:09 CST
- Generated at (UTC): 2026-03-02 14:20:09 UTC
- Branch: `main`
- Commit: `b438cb4bdf27a1dd76c7052bed7747f8679ff311`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir bench/runs/baseline-2026-03-02`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..200` (`200` users)
- Messages per sender: `200`
- Send interval: `1.0 ms`
- Group ID: `1 (21 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 200 | 200 | 13.3/s | 7486.0 | 14208.3 | 14819.6 | 12.5% |
| One-to-One Chat | 100 | 20000 | 417.1/s | 7890.9 | 13639.4 | 13677.1 | 96.6% |
| Group Chat (1->20) | 21 | 200 | 0.0/s | 0.0 | 0.0 | 0.0 | 2000.0% |

## 4. Environment Metadata
- Metadata file: `bench/runs/baseline-2026-03-02/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-02/meta.md`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:
执行完`bench/run.py`之后，客户端已经停止了，但是启动的两个服务端都还在一直不停地刷这种log：
````                                                                                                                           
  20260302 14:22:14.086275Z 3290492 INFO  /home/Jade/chat-server/github/Cluster-Chat-Server-With-CPP/src/server/db/db.cpp:36:    
  connect mysql success! - db.cpp:36                                                                                            
```       

并且服务端还出现了这种带错误的log：
```
  20260302 14:21:05.180384Z 3290494 ERROR Broken pipe (errno=32) TcpConnection::sendInLoop - TcpConnection.cc:167      
  20260302 14:22:11.568734Z 3290462 ERROR TcpConnection::handleError [ChatServer-127.0.0.:6000#139] - SO_ERROR = 32 Broken pipe 
```

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
增加MySQL max_connections 的值，目前设为500