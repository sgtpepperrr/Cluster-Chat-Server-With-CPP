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

- `chat --count 750 --messages 200 --interval 0.001` repeated 5 times: all 5 runs `0 error`
- `chat --count 750 --messages 200 --interval 0.002` repeated 5 times: all 5 runs `0 error`

Interpretation:

- In current environment, `1ms` is not showing errors in repeated short runs.
- Keep treating `1ms` as edge condition; continue using `2ms` as conservative steady-state setting for regression checks.

## Next Optimization Focus

Based on current results, the next bottleneck target is **group fan-out latency under 1->100**:

- `Group(1->100) P95 ~= 6433.8 ms` (while error rate is already `0.0%`)

Recommended next code optimizations:

1. shrink `connMutex_` critical section in `groupChat`
2. remove per-recipient `userModel_.query(id)` pattern (batch state query)
3. apply same lock-scope cleanup in `handleRedisSubscribeMessage`
