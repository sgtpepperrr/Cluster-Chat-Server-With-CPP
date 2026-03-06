# Round 2 Regression After Optimization

## Goal

Validate round-2 changes (removing online-state DB checks from message hot path) and quantify impact on chat/group performance.

## 1) Chat Main Metric

Command:

```bash
python3 bench/run.py chat --port 8000 --start-id 1 --count 750 --messages 200 --interval 0.002
```

====================================================
  Benchmark Configuration
====================================================
  Target:           127.0.0.1:8000
  User IDs:         1 .. 750  (750 users)
  Messages/sender:  200
  Send interval:    2.0 ms
  Login timeout:    15.0 s
====================================================
[chat] 375 pairs x 200 msgs (interval 2 ms) ...

====================================================
  One-to-One Chat
====================================================
  Concurrency:      375
  Total requests:   75000
  Duration:         7.29 s
  Throughput:       10288.4 req/s
  Latency min:      11.10 ms
  Latency P50:      1280.90 ms
  Latency P95:      3240.24 ms
  Latency P99:      4656.87 ms
  Latency max:      5521.12 ms
  Successes:        75000
  Errors:           0
  Disconnects:      0
  Error rate:       0.0%
====================================================

---

## 2) Group Main Metric (fan-out P95)

Command:

```bash
python3 bench/run.py group --port 8000 --start-id 201 --count 101 --group-id 2 --group-members 101 --messages 500 --interval 0.001
```

====================================================
  Benchmark Configuration
====================================================
  Target:           127.0.0.1:8000
  User IDs:         201 .. 301  (101 users)
  Messages/sender:  500
  Send interval:    1.0 ms
  Login timeout:    15.0 s
  Group ID:         2  (101 members)
====================================================
[group] 1 sender -> 100 receivers, 500 msgs, group 2 ...

====================================================
  Group Chat (1->100)
====================================================
  Concurrency:      101
  Total requests:   500
  Duration:         12.63 s
  Throughput:       39.6 req/s
  Latency min:      0.69 ms
  Latency P50:      15.48 ms
  Latency P95:      47.96 ms
  Latency P99:      67.26 ms
  Latency max:      75.75 ms
  Successes:        50000
  Errors:           0
  Disconnects:      0
  Error rate:       0.0%
  --- Fan-out (100 receivers) ---
  Complete msgs:    500/500
  Fan-out P50:      18.88 ms
  Fan-out P95:      51.65 ms
  Fan-out P99:      70.59 ms
====================================================

## Conclusion

Compared with **round1-regression-after-optimization.md**:

- One-to-one chat (`count=750`, `interval=2ms`):
  - Throughput: `7216.3/s -> 10288.4/s` (**+42.6%**)
  - Latency P50: `2887.68 ms -> 1280.90 ms` (**-55.6%**)
  - Latency P95: `6369.06 ms -> 3240.24 ms` (**-49.1%**)
  - Latency P99: `7695.14 ms -> 4656.87 ms` (**-39.5%**)
  - Error rate: still `0.0%`

- Group chat (`1->100`, `interval=1ms`):
  - Throughput: `38.2/s -> 39.6/s` (**+3.7%**)
  - Latency P95: `66.33 ms -> 47.96 ms` (**-27.7%**)
  - Fan-out P95: `71.93 ms -> 51.65 ms` (**-28.2%**)
  - Fan-out P99: `80.66 ms -> 70.59 ms` (**-12.5%**)
  - Complete msgs: still `500/500`, error rate still `0.0%`

Assessment:

- Round 2 is effective and measurable.
- The remaining primary bottleneck is still one-to-one chat tail latency at high concurrency (P95/P99 still in seconds).
