# Benchmark Report

## 1. Run Snapshot
- Generated at (local): 2026-03-06 10:29:06 CST
- Generated at (UTC): 2026-03-06 02:29:06 UTC
- Branch: `main`
- Commit: `1626c89c3cafe93ca1717ae2eaeacbcbd4d5a229`
- Scenario: `login`
- Command: `python3 bench/run.py login --port 8000 --start-id 1 --count 800 --report-file bench/runs/pool-v1-2026-03-06/03-login-c800.md`

## 2. Test Setup
- Target: `127.0.0.1:8000`
- User IDs: `1..800` (`800` users)
- Messages per sender: `100`
- Send interval: `1.0 ms`
- Login timeout: `15.0 s`
- Group ID: `N/A`

## 3. Results
| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |
|----------|-------------|----------|------------|-----|-----|-----|------------|
| Login Throughput | 800 | 800 | 476.9/s | 724.8 | 1186.6 | 1414.0 | 7.1% |

## 3.1 Login Error Breakdown
### Login Throughput
- `connection_error`: 57


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
