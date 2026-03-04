# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-04 16:32:00 CST
- Generated at (UTC): 2026-03-04 08:32:00 UTC
- Branch: `main`
- Commit: `b783cb1a5be7cbdd13524384aafeb49adc9871b6`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --interval 1 --start-id 1 --count 170 --group-id 1 --messages 200 --run-dir bench/runs/baseline-2026-03-04`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..170` (`170` users)
- Messages per sender: `200`
- Send interval: `1000.0 ms`
- Login timeout: `15.0 s`
- Group ID: `1 (21 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 170 | 170 | 11.3/s | 7425.5 | 13883.3 | 14506.3 | 0.6% |
| One-to-One Chat | 85 | 17000 | 78.5/s | 39.8 | 536.3 | 4898.3 | 0.0% |
| Group Chat (1->20) | 21 | 200 | 1.0/s | 170.0 | 312.1 | 331.2 | 0.0% |

## 3.1 Login Error Breakdown
### Login Throughput
- `recv_timeout`: 1


## 4. Environment Metadata
- Metadata file: `bench/runs/baseline-2026-03-04/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-04/meta.md`

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
进行过如下测试：
200 -> 175: 5.1%
175 -> 150: 0.0%
150 -> 160: 0.0%
160 -> 175: 1.1%
175 -> 170: 0.0%

基本上就是170左右的并发量了。

### 2. 对于chat的并发性能
interval低于500ms的话，完全无法承受；
500ms-1s之间，没有测试；
1s以上，基本没有问题 
