# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-06 10:31:11 CST
- Generated at (UTC): 2026-03-06 02:31:11 UTC
- Branch: `main`
- Commit: `1626c89c3cafe93ca1717ae2eaeacbcbd4d5a229`
- Scenario: `chat`
- Command: `python3 bench/run.py chat --port 8000 --start-id 1 --count 750 --messages 200 --interval 0.002 --report-file bench/runs/pool-v1-2026-03-06/05b-chat-c750-i2ms.md`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..750` (`750` users)
- Messages per sender: `200`
- Send interval: `2.0 ms`
- Login timeout: `15.0 s`
- Group ID: `N/A`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| One-to-One Chat | 375 | 75000 | 7122.7/s | 4628.8 | 7412.0 | 8328.5 | 0.0% |


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
