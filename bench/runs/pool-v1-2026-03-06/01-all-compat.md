# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-06 10:08:38 CST
- Generated at (UTC): 2026-03-06 02:08:38 UTC
- Branch: `main`
- Commit: `1626c89c3cafe93ca1717ae2eaeacbcbd4d5a229`
- Scenario: `all`
- Command: `python3 bench/run.py all --port 8000 --start-id 1 --count 170 --group-id 1 --messages 200 --interval 1 --report-file bench/runs/pool-v1-2026-03-06/01-all-compat.md`

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
| Login Throughput | 170 | 170 | 198.5/s | 678.2 | 784.4 | 796.3 | 0.0% |
| One-to-One Chat | 85 | 17000 | 84.8/s | 2.9 | 9.3 | 11.9 | 0.0% |
| Group Chat (1->20) | 21 | 200 | 1.0/s | 2.7 | 4.1 | 4.5 | 0.0% |


## 4. Environment Metadata
- Metadata file: `bench/runs/<run-id>/meta.md`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh bench/runs/<run-id>/meta.md`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
