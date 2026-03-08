# oneChat 线程调优结论

## 背景

在完成以下优化之后：

- Redis 从消息转发改为控制面路由
- 节点间 oneChat 改为 TCP 直连 + 轻量内部帧
- ClusterRouter 增加背压缓冲
- 本地连接表改为分片锁
- 本地投递改为按 EventLoop 分桶
- 增加 oneChat 分阶段指标

单聊尾延迟的主要瓶颈已经不再是 Redis 或节点间路由，而是**目标客户端连接所属 EventLoop 的排队等待**。

这一点可以从阶段指标直接看出来：

- `resolve_avg_us`：约 `75 ~ 217 us`
- `remote_send_avg_us`：约 `106 ~ 144 us`
- `remote_handle_avg_us`：约 `6 ~ 11 us`
- `local_send_avg_us`：约 `58 ~ 115 us`
- `local_queue_wait_avg_us`：约 `45,590 ~ 124,750 us`

也就是说，真正大的时间开销不在“查路由”“跨节点发送”或“真正调用 send”，而在**目标 loop 的排队等待**。

## 对比基线说明

本文主要使用 `bench/runs/pool-v1-2026-03-06/round3-regression-after-optimization.md` 作为对比基线。

原因是：

- `round3` 是旧架构阶段的**最后一个稳定版本**
- 对应场景与当前主对比场景一致：
  - `chat --count 750 --messages 200 --interval 0.002`
- 它比更早的 baseline 更公平，因为已经包含了旧架构下的一轮关键优化，而不是拿非常早期的结果来放大收益

`round3` 的 oneChat 结果为：

- Throughput：`9442.9 req/s`
- Latency P50：`413.65 ms`
- Latency P95：`1405.54 ms`
- Latency P99：`1727.40 ms`
- Errors：`0`
- Error rate：`0.0%`

## 测试结论

以下结论都基于当前测试机器（约 2 核）上的实际压测结果。

### 1. 旧架构最终基线：`round3`

配置背景：

- 仍属于旧的消息路径方案
- 对应 `bench/runs/pool-v1-2026-03-06/round3-regression-after-optimization.md`

结果：

- Throughput：`9442.9 req/s`
- Latency P50：`413.65 ms`
- Latency P95：`1405.54 ms`
- Latency P99：`1727.40 ms`
- Errors：`0`
- Error rate：`0.0%`

这组数据作为后续新架构优化的主要比较基线。

### 2. 稳定推荐配置：`6 / 2 / 2`

配置：

- `CHAT_SERVER_IO_THREADS=6`
- `CHAT_CLUSTER_IO_THREADS=2`
- `CHAT_INTER_NODE_IO_THREADS=2`

对应压测结果：

- Throughput：`9116.8 req/s`
- Latency P50：`67.20 ms`
- Latency P95：`871.28 ms`
- Latency P99：`1053.60 ms`
- Latency max：`1316.77 ms`
- Errors：`0`
- Error rate：`0.0%`

对应阶段指标：

- `local_queue_wait_avg_us`：`70772 us`（server1） / `77427 us`（server2）
- `local_queue_wait_max_us`：`728200 us` / `612176 us`
- `local_dispatch_peak`：`1263` / `1273`

相比旧架构 `round3`：

- Throughput：`9442.9 -> 9116.8 req/s`，下降约 **3.5%**
- P50：`413.65 -> 67.20 ms`，下降约 **83.8%**
- P95：`1405.54 -> 871.28 ms`，下降约 **38.0%**
- P99：`1727.40 -> 1053.60 ms`，下降约 **39.0%**
- Error rate：保持 `0.0%`

结论：

- 这组配置在保持 `0 error` 的前提下，把 oneChat 尾延迟进一步压低到了亚秒级
- 相比旧架构最终基线，吞吐略有回落，但 P50/P95/P99 都有明显改善
- 它是当前 2 核机器上的**稳定推荐配置**

### 3. 激进性能配置：`8 / 4 / 4`

配置：

- `CHAT_SERVER_IO_THREADS=8`
- `CHAT_CLUSTER_IO_THREADS=4`
- `CHAT_INTER_NODE_IO_THREADS=4`

对应压测结果：

- Throughput：`10038.6 req/s`
- Latency P50：`56.12 ms`
- Latency P95：`542.46 ms`
- Latency P99：`677.07 ms`
- Latency max：`960.17 ms`
- Errors：`1400`
- Error rate：`1.9%`

对应阶段指标：

- `local_queue_wait_avg_us`：`50322 us`（server1） / `45590 us`（server2）
- `local_queue_wait_max_us`：`574641 us` / `462842 us`
- `local_dispatch_peak`：`640` / `609`

相比旧架构 `round3`：

- Throughput：`9442.9 -> 10038.6 req/s`，提升约 **6.3%**
- P50：`413.65 -> 56.12 ms`，下降约 **86.4%**
- P95：`1405.54 -> 542.46 ms`，下降约 **61.4%**
- P99：`1727.40 -> 677.07 ms`，下降约 **60.8%**
- Error rate：`0.0% -> 1.9%`

相比 `6 / 2 / 2`：

- P95：`871.28 -> 542.46 ms`，下降约 **37.7%**
- P99：`1053.60 -> 677.07 ms`，下降约 **35.7%**
- 但新增了 `1.9%` error

结论：

- 这是当前测试里**性能上限最高**的一组线程配置
- 它进一步压低了 oneChat 的 P95/P99
- 但在 2 核机器上线程数偏多，开始影响稳定性，因此更适合做**性能上限参考**，不适合作为默认配置

## 总结

当前 2 核测试环境下，可以把线程调优结论归纳为：

- 旧架构 `round3`：`P95 1405.54 ms / P99 1727.40 ms / 0 error`
- 新架构稳定配置 `6 / 2 / 2`：`P95 871.28 ms / P99 1053.60 ms / 0 error`
- 新架构激进配置 `8 / 4 / 4`：`P95 542.46 ms / P99 677.07 ms / 1.9% error`

因此：

- 如果目标是**稳定性优先**，推荐使用 `6 / 2 / 2`
- 如果目标是**性能上限验证**，可以使用 `8 / 4 / 4` 作为参考

从阶段指标看，线程数增加后，`local_queue_wait_avg_us` 会明显下降；这进一步验证了前面的判断：**oneChat 当前的主瓶颈，就是目标客户端连接所属 EventLoop 的排队等待。**


## 可以这样总结这轮优化：

> 我没有拿很早期的 baseline 来对比，而是选了旧架构最后一个稳定版本 round3 作为基线。在同样的 `count=750、messages=200、interval=2ms` 条件下，旧架构 round3 的结果是 `P95 1405 ms / P99 1727 ms / 0 error`。改成 Redis 控制面 + 节点直连数据面之后，我继续做了轻量协议、背压缓冲、本地连接表分片和按 EventLoop 分桶的本地异步投递。最后在 2 核机器上做线程调优，稳定配置 `6/2/2` 可以做到 `P95 871 ms / P99 1054 ms / 0 error`，相比旧架构分别下降约 `38%` 和 `39%`；更激进的 `8/4/4` 可以进一步压到 `P95 542 ms / P99 677 ms`，但会带来 `1.9%` error。因此我把 `6/2/2` 作为当前环境下的稳定推荐配置，把 `8/4/4` 作为性能上限参考。
