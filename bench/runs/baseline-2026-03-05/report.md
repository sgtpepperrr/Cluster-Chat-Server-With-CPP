# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-05 23:15:49 CST
- Generated at (UTC): 2026-03-05 15:15:49 UTC
- Branch: `main`
- Commit: `b39edefd0163c0f3f0934a36b3132bdbdd5ed1c7`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --interval 0.001 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir bench/runs/baseline-2026-03-05`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..200` (`200` users)
- Messages per sender: `200`
- Send interval: `1.0 ms`
- Login timeout: `15.0 s`
- Group ID: `1 (21 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 200 | 200 | 472.4/s | 147.9 | 276.0 | 351.1 | 0.0% |
| One-to-One Chat | 100 | 20000 | 8169.3/s | 469.4 | 1139.0 | 1643.4 | 0.0% |
| Group Chat (1->20) | 21 | 200 | 19.5/s | 181.9 | 347.8 | 361.9 | 0.0% |


## 4. Environment Metadata
- Metadata file: `bench/runs/baseline-2026-03-05/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-05/meta.md`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
### 1. 对于login的并发性能
200的并发量可以轻松通过测试。

### 2. 对于chat的并发性能
这个并发量下，1ms也完全没问题。