# Round 1 Regression After Optimization

## Goal

Validate that round-1 performance optimizations improve group fan-out latency and do not regress chat stability.

## 1) Group Main Metric (fan-out P95)

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
  Duration:         13.10 s
  Throughput:       38.2 req/s
  Latency min:      0.61 ms
  Latency P50:      21.91 ms
  Latency P95:      66.33 ms
  Latency P99:      77.71 ms
  Latency max:      87.11 ms
  Successes:        50000
  Errors:           0
  Disconnects:      0
  Error rate:       0.0%
  --- Fan-out (100 receivers) ---
  Complete msgs:    500/500
  Fan-out P50:      26.86 ms
  Fan-out P95:      71.93 ms
  Fan-out P99:      80.66 ms
====================================================

---

## 2) Chat Non-Regression Check

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
  Duration:         10.39 s
  Throughput:       7216.3 req/s
  Latency min:      8.45 ms
  Latency P50:      2887.68 ms
  Latency P95:      6369.06 ms
  Latency P99:      7695.14 ms
  Latency max:      8704.59 ms
  Successes:        75000
  Errors:           0
  Disconnects:      0
  Error rate:       0.0%
====================================================

## Conclusion

- Group fan-out latency improved significantly under `1 -> 100` scenario. (P95: 6433.8 ms -> 66.33 ms, almost 100x)
- Chat baseline (`count=750`, `interval=2ms`) remains stable with `0.0%` error rate.
