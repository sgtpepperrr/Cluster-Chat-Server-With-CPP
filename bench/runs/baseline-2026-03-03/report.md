# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-03 22:51:26 CST
- Generated at (UTC): 2026-03-03 14:51:26 UTC
- Branch: `main`
- Commit: `b438cb4bdf27a1dd76c7052bed7747f8679ff311`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir bench/runs/baseline-2026-03-03`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..200` (`200` users)
- Messages per sender: `200`
- Send interval: `1.0 ms`
- Group ID: `1 (21 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 200 | 200 | 13.3/s | 7471.8 | 14157.9 | 14729.7 | 11.5% |
| One-to-One Chat | 100 | 20000 | 404.4/s | 5423.1 | 11733.0 | 11786.5 | 94.7% |
| Group Chat (1->20) | 21 | 200 | 0.0/s | 0.0 | 0.0 | 0.0 | 2000.0% |

## 4. Environment Metadata
- Metadata file: `bench/runs/baseline-2026-03-03/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-03/meta.md`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
