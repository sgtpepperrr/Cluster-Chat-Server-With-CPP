# Pool v1 Baseline Summary (2026-03-06)

## Scope

This summary captures the first benchmark baseline **after introducing MySQL connection pool**.

- New baseline run directory: `bench/runs/pool-v1-2026-03-06`
- Primary comparison target (before pool, same all-compat command): `bench/runs/baseline-2026-03-04/report.md`

## Result Files (this run)

- `01-all-compat.md`
- `02-login-c750.md`
- `03-login-c800.md`
- `04-chat-c750-i5ms.md`
- `05-chat-c750-i1ms.md`
- `05b-chat-c750-i2ms.md`
- `06-group101-i1ms.md`

## Key Results (Pool v1)

| Scenario | Params | Throughput | P95 | Error Rate | Notes |
|---|---|---:|---:|---:|---|
| All compat - Login | `all, count=170, interval=1s` | `198.5/s` | `784.4 ms` | `0.0%` | same command as 2026-03-04 baseline |
| All compat - Chat | `all, count=170, interval=1s` | `84.8/s` | `9.3 ms` | `0.0%` | same command as 2026-03-04 baseline |
| All compat - Group(1->20) | `all, group=21, interval=1s` | `1.0/s` | `4.1 ms` | `0.0%` | same command as 2026-03-04 baseline |
| Login pass line | `login, count=750` | `490.2/s` | `1268.8 ms` | `0.0%` | stable |
| Login edge fail | `login, count=800` | `476.9/s` | `1186.6 ms` | `7.1%` | `connection_error=57` |
| Chat capacity line | `chat, count=750, messages=200, interval=5ms` | `6544.5/s` | `6959.3 ms` | `0.0%` | stable |
| Chat rate edge | `chat, count=750, messages=200, interval=1ms` | `7213.9/s` | `6790.0 ms` | `0.0%` | this run has no error |
| Chat control | `chat, count=750, messages=200, interval=2ms` | `7122.7/s` | `7412.0 ms` | `0.0%` | control point |
| Group 1->100 | `group, count=101, messages=500, interval=1ms` | `47.1/s` | `6433.8 ms` | `0.0%` | fan-out complete `500/500` |

## Before/After (Same Command, Most Important)

Command compared:

```bash
python3 bench/run.py all --port 8000 --interval 1 --start-id 1 --count 170 --group-id 1 --messages 200
```

| Scenario | Before Pool (2026-03-04) | After Pool v1 (2026-03-06) | Change |
|---|---:|---:|---|
| Login throughput | `11.3/s` | `198.5/s` | ~`17.6x` higher |
| Login P95 | `13883.3 ms` | `784.4 ms` | major drop |
| Login error rate | `0.6%` | `0.0%` | improved |
| Chat throughput | `78.5/s` | `84.8/s` | slightly higher |
| Chat P95 | `536.3 ms` | `9.3 ms` | major drop |
| Group(1->20) throughput | `1.0/s` | `1.0/s` | unchanged (interval-bound) |
| Group(1->20) P95 | `312.1 ms` | `4.1 ms` | major drop |

## Stability Note (manual rerun)

Additional stability validation was executed manually after the 6 saved cases:

- Detailed record: `round1-regression-after-optimization.md`
- Round-2 detailed record: `round2-regression-after-optimization.md`

- `chat --count 750 --messages 200 --interval 0.001` repeated 5 times: all 5 runs `0 error`
- `chat --count 750 --messages 200 --interval 0.002` repeated 5 times: all 5 runs `0 error`

Interpretation:

- In current environment, `1ms` is not showing errors in repeated short runs.
- Keep treating `1ms` as edge condition; continue using `2ms` as conservative steady-state setting for regression checks.

## Next Optimization Focus

Based on current results, the next bottleneck target is **one-to-one chat tail latency under high concurrency**:

- `chat (count=750, interval=2ms) P95 ~= 3240.24 ms` after round 2

Recommended next code optimizations:

1. move `oneChat` local `send` out of `connMutex_` critical section
2. evaluate event-loop worker thread count (`2` vs `4`) with controlled A/B test on current host
3. keep group-chat path unchanged in next round and avoid mixing variables

## Round 1 Delta (vs Initial Pool-v1 Saved Cases)

Round-1 optimization compared with initial pool-v1 saved runs (`05b-chat-c750-i2ms.md`, `06-group101-i1ms.log`):

- `chat (count=750, interval=2ms)`:
  - Throughput: `7122.7/s -> 7216.3/s` (**+1.3%**)
  - P50 latency: `4628.8 ms -> 2887.68 ms` (**-37.6%**)
  - P95 latency: `7412.0 ms -> 6369.06 ms` (**-14.1%**)
  - P99 latency: `8328.5 ms -> 7695.14 ms` (**-7.6%**)
- `group (1->100, interval=1ms)`:
  - Throughput: `47.1/s -> 38.2/s` (**-18.9%**)
  - P95 latency: `6433.77 ms -> 66.33 ms` (**-99.0%**)
  - P99 latency: `6694.80 ms -> 77.71 ms` (**-98.8%**)
  - Fan-out P95: `6434.82 ms -> 71.93 ms` (**-98.9%**)
  - Fan-out P99: `6695.47 ms -> 80.66 ms` (**-98.8%**)

## Round 2 Delta (vs Round 1 Regression)

With round-2 message-path optimization:

- `chat (count=750, interval=2ms)`:
  - Throughput: `7216.3/s -> 10288.4/s` (**+42.6%**)
  - P50 latency: `2887.68 ms -> 1280.90 ms` (**-55.6%**)
  - P95 latency: `6369.06 ms -> 3240.24 ms` (**-49.1%**)
  - P99 latency: `7695.14 ms -> 4656.87 ms` (**-39.5%**)
- `group (1->100, interval=1ms)`:
  - Throughput: `38.2/s -> 39.6/s` (**+3.7%**)
  - P95 latency: `66.33 ms -> 47.96 ms` (**-27.7%**)
  - P99 latency: `77.71 ms -> 67.26 ms` (**-13.4%**)
  - Fan-out P95: `71.93 ms -> 51.65 ms` (**-28.2%**)
  - Fan-out P99: `80.66 ms -> 70.59 ms` (**-12.5%**)

Current remaining bottleneck is still one-to-one chat tail latency under high concurrency.
