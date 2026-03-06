# Round 3 Regression After Optimization

## Goal

Validate the impact of moving local `oneChat` send operation out of `connMutex_` critical section.

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
  Duration:         7.94 s
  Throughput:       9442.9 req/s
  Latency min:      2.42 ms
  Latency P50:      413.65 ms
  Latency P95:      1405.54 ms
  Latency P99:      1727.40 ms
  Latency max:      2109.50 ms
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
  Duration:         12.73 s
  Throughput:       39.3 req/s
  Latency min:      0.56 ms
  Latency P50:      9.13 ms
  Latency P95:      29.82 ms
  Latency P99:      53.87 ms
  Latency max:      65.91 ms
  Successes:        50000
  Errors:           0
  Disconnects:      0
  Error rate:       0.0%
  --- Fan-out (100 receivers) ---
  Complete msgs:    500/500
  Fan-out P50:      12.42 ms
  Fan-out P95:      35.00 ms
  Fan-out P99:      59.07 ms
====================================================

## Conclusion

Compared with **round2-regression-after-optimization.md**:

- One-to-one chat (`count=750`, `interval=2ms`):
  - Throughput: `10288.4/s -> 9442.9/s` (**-8.2%**)
  - Latency P50: `1280.90 ms -> 413.65 ms` (**-67.7%**)
  - Latency P95: `3240.24 ms -> 1405.54 ms` (**-56.6%**)
  - Latency P99: `4656.87 ms -> 1727.40 ms` (**-62.9%**)
  - Error rate: still `0.0%`

- Group chat (`1->100`, `interval=1ms`):
  - Throughput: `39.6/s -> 39.3/s` (**-0.8%**)
  - Latency P95: `47.96 ms -> 29.82 ms` (**-37.8%**)
  - Latency P99: `67.26 ms -> 53.87 ms` (**-19.9%**)
  - Fan-out P95: `51.65 ms -> 35.00 ms` (**-32.2%**)
  - Fan-out P99: `70.59 ms -> 59.07 ms` (**-16.3%**)
  - Complete msgs: still `500/500`, error rate still `0.0%`

Assessment:

- Round 3 further reduced tail latency significantly.
- The next likely bottleneck is Redis publish path serialization (`publish_mtx_` + single publish context), not DB or `connMutex_` lock scope.
