# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-06 10:33:05 CST
- Generated at (UTC): 2026-03-06 02:33:05 UTC
- Branch: `main`
- Commit: `1626c89c3cafe93ca1717ae2eaeacbcbd4d5a229`
- Scenario: `group`
- Command: `python3 bench/run.py group --port 8000 --start-id 201 --count 101 --group-id 2 --group-members 101 --messages 500 --interval 0.001 --report-file bench/runs/pool-v1-2026-03-06/06-group101-i1ms.md`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `201..301` (`101` users)
- Messages per sender: `500`
- Send interval: `1.0 ms`
- Login timeout: `15.0 s`
- Group ID: `2 (101 members)`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Group Chat (1->100) | 101 | 500 | 47.1/s | 3409.7 | 6433.8 | 6694.8 | 0.0% |


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
