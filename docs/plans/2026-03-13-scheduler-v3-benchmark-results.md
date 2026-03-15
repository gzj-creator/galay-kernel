# Scheduler V3 Benchmark Results

## Environment

- Date: 2026-03-13
- Host: macOS
- Compiler: AppleClang 17
- Backend: kqueue
- Build: Release-like default CMake build
- Baseline source: `cde3da1`
- V3 source: current `v3` worktree

## Commands

### V3

```bash
cmake -S . -B build
cmake --build build --target \
  T1-coroutine_chain T25-spawn_simple T27-runtime_stress \
  B1-ComputeScheduler B6-Udp B8-MpscChannel B13-Sendfile \
  B14-SchedulerInjectedWakeup -j4

./build/bin/T1-coroutine_chain
./build/bin/T25-spawn_simple
./build/bin/T27-runtime_stress
./build/bin/B14-SchedulerInjectedWakeup
./build/bin/B1-ComputeScheduler
./build/bin/B8-MpscChannel
./build/bin/B6-Udp
./build/bin/B13-Sendfile
```

### Baseline

```bash
git worktree add .worktrees/v3-baseline cde3da1
cmake -S .worktrees/v3-baseline -B .worktrees/v3-baseline/build
cmake --build .worktrees/v3-baseline/build --target \
  T1-coroutine_chain T25-spawn_simple T27-runtime_stress \
  B1-ComputeScheduler B6-Udp B8-MpscChannel B13-Sendfile -j4

./.worktrees/v3-baseline/build/bin/T1-coroutine_chain
./.worktrees/v3-baseline/build/bin/T25-spawn_simple
./.worktrees/v3-baseline/build/bin/T27-runtime_stress
./.worktrees/v3-baseline/build/bin/B1-ComputeScheduler
./.worktrees/v3-baseline/build/bin/B8-MpscChannel
./.worktrees/v3-baseline/build/bin/B6-Udp
./.worktrees/v3-baseline/build/bin/B13-Sendfile
```

### Baseline `B14`

`B14` 为 `v3` 新增 benchmark，因此通过当前 `B14` 源文件链接 baseline `libgalay-kernel` 生成临时可执行文件进行对比采样。

## Correctness

- `T1-coroutine_chain`: PASS on `v3`
- `T25-spawn_simple`: PASS on `v3`
- `T27-runtime_stress`: PASS on `v3`
- `T36-task_core_wake_path`: PASS on `v3`
- `T39-awaitable_hot_path_helpers`: PASS on `v3`
- `T41-task_ref_schedule_path`: PASS on `v3`
- `T43-wait_continuation_scheduler_path`: PASS on `v3`
- `T44-scheduler_wakeup_coalescing`: PASS on `v3`
- `T45-scheduler_ready_budget`: PASS on `v3`

## Summary Table

| Benchmark | Metric | Baseline | V3 | Delta |
|---|---|---:|---:|---:|
| `T27-runtime_stress` | concurrent submit time | 116 ms | 62 ms | -46.6% |
| `T27-runtime_stress` | scheduler getter throughput | 531M ops/s | 571M ops/s | +7.5% |
| `B1-ComputeScheduler` | empty throughput | 892,857 task/s | 892,857 task/s | 0.0% |
| `B1-ComputeScheduler` | light throughput | 961,538 task/s | 952,381 task/s | -1.0% |
| `B1-ComputeScheduler` | latency | 85.01 us | 48.65 us | -42.8% |
| `B8-MpscChannel` | single producer throughput | 10.42M msg/s | 10.75M msg/s | +3.2% |
| `B8-MpscChannel` | multi producer throughput | 17.86M msg/s | 18.87M msg/s | +5.7% |
| `B8-MpscChannel` | batch throughput | 18.87M msg/s | 16.39M msg/s | -13.1% |
| `B8-MpscChannel` | latency | 1127.08 us | 1414.26 us | +25.5% |
| `B8-MpscChannel` | cross-scheduler throughput | 14.29M msg/s | 12.99M msg/s | -9.1% |
| `B8-MpscChannel` | sustained throughput | 10.49M msg/s | 11.19M msg/s | +6.7% |
| `B6-Udp` | packet loss | 0.5215% | 0.2290% | improved |
| `B6-Udp` | send throughput | 8.55 MB/s | 8.55 MB/s | roughly equal |
| `B6-Udp` | recv throughput | 8.50 MB/s | 8.53 MB/s | +0.3% |
| `B13-Sendfile` | 1 MB sendfile throughput | 14.93 MB/s | 15.63 MB/s | +4.7% |
| `B13-Sendfile` | 10 MB sendfile throughput | 156.25 MB/s | 151.52 MB/s | -3.0% |
| `B13-Sendfile` | 50 MB sendfile throughput | 649.35 MB/s | 632.91 MB/s | -2.5% |
| `B13-Sendfile` | 100 MB sendfile throughput | 1123.60 MB/s | 1265.82 MB/s | +12.7% |

## `B14` Injected Wakeup Benchmark

由于 `B14` 为新增 benchmark，记录两次样本区间：

| Metric | Baseline Samples | V3 Samples | Observation |
|---|---|---|---|
| injected throughput | 1.27M–1.42M task/s | 4.88M–6.67M task/s | `v3` 显著提升 |
| injected avg latency | 4.32–7.25 us | 169.86–233.00 us | `v3` 明显回退 |

## Interpretation

### Positive

- `wait` continuation 经 scheduler 路径恢复后，相关 correctness 回归全部通过
- injected-heavy 路径吞吐显著提升，`B14` 是最直观证据
- `T27` 高并发提交时间明显缩短
- `B1` 调度延迟改善明显
- UDP 包丢失率下降

### Neutral / Mixed

- `sendfile` 结果受文件大小和缓存波动影响较大，整体没有一致退化
- `B8` 中单生产者 / 多生产者 / sustained throughput 提升，但 batch 和 cross-scheduler 路径回退

### Negative

- `B14` injected 平均延迟出现明显回退
- `B8` 延迟也出现回退

## Current Hypothesis

`v3` 当前实现把 wakeup coalescing 和 ready budget 都压向了吞吐友好方向：

- remote enqueue syscall 明显减少，带来 injected throughput 提升
- 但任务恢复更倾向于批处理，平均排队等待时间上升，导致 injected latency 回退

下一轮可优先考虑：

1. 细化 `sleeping / wakeup_pending` 状态机
2. 调小 ready budget，或按 injected backlog 做动态预算
3. 区分 throughput 模式和 latency 模式的调度参数

## Conclusion

`v3` 当前版本在 correctness 和 injected throughput 上已经明显优于 baseline，但 latency 面仍有退化。  
如果目标是 “更稳的语义 + 更高 injected 吞吐”，当前结果是正向的；如果目标包含 “不牺牲 injected latency”，则还需要继续调度参数与唤醒策略。

## Latency Tuning Follow-up (2026-03-13)

### Additional scheduler changes

在首轮 `v3` 结果基础上，继续做了两项 latency-oriented tuning：

1. **Injected burst credit**
   - 当 `processPendingCoroutines()` 一轮开始时没有本地 ready work，允许当前 pass 继续消费本轮 drain 到的 injected backlog
   - 保留 local-ready budget，避免 local self-reschedule 无限逃逸公平性边界

2. **Injected queue edge-triggered wakeup**
   - 为 worker 增加 `injected_outstanding`
   - remote enqueue 在 injected queue 从空变非空时，即使 scheduler 尚未标记为 `sleeping`，也会补发一次 coalesced wakeup
   - 保留 `wakeup_pending`，因此仍然不是“每个 injected task 都唤醒一次”

### Additional correctness coverage

- `T45-SchedulerReadyBudget`: 改为约束 local ready backlog 的 budget 行为
- `T46-SchedulerInjectedBurstFastPath`: 约束 pure injected backlog 可在单轮 pass 内推进完成
- `T47-SchedulerQueueEdgeWakeup`: 约束 injected queue 空 -> 非空的 edge wakeup 行为

### Verification

Passing tests:
- `T27-runtime_stress`
- `T39-awaitable_hot_path_helpers`
- `T43-wait_continuation_scheduler_path`
- `T44-scheduler_wakeup_coalescing`
- `T45-scheduler_ready_budget`
- `T46-scheduler_injected_burst_fastpath`
- `T47-scheduler_queue_edge_wakeup`

### Post-tuning benchmark samples

#### `B14-SchedulerInjectedWakeup`

Observed samples after latency tuning:

| Metric | Samples | Compare to prior `v3` | Compare to baseline |
|---|---|---|---|
| injected throughput | `4.26M`, `5.41M`, `5.13M`, `6.06M` task/s | still far above prior baseline-comparison `v3` (`4.88M–6.67M`) order of magnitude | still far above baseline `1.27M–1.42M` |
| injected avg latency | `3.07us`, `1.27us`, `1.66us`, `11.11us` | **major recovery** from prior `v3` `169.86us–233.00us` | back to baseline class, and in some samples lower than baseline `4.32us–7.25us` |

Interpretation:
- `injected latency` 的主回归点并不只是 fixed ready budget，还包括 injected queue 的唤醒边界过保守
- 加入 edge-triggered wakeup 后，`B14` latency 已经回到 baseline 量级，同时保住了远高于 baseline 的 throughput

#### `B8-MpscChannel`

Two follow-up samples showed:

| Metric | Samples | Prior `v3` | Baseline |
|---|---|---:|---:|
| single producer throughput | `10.75M`, `13.89M` msg/s | `10.75M` | `10.42M` |
| multi producer throughput | `17.86M`, `16.95M` msg/s | `18.87M` | `17.86M` |
| batch throughput | `18.52M`, `16.39M` msg/s | `16.39M` | `18.87M` |
| latency | `29.22us`, `1271.51us` | `1414.26us` | `1127.08us` |
| cross-scheduler throughput | `12.82M`, `14.08M` msg/s | `12.99M` | `14.29M` |
| sustained throughput | `11.39M`, `11.38M` msg/s | `11.19M` | `10.49M` |

Interpretation:
- `cross-scheduler throughput` 明显回收，已经重新逼近 baseline
- `latency` 指标仍然波动较大，当前不能据此宣称已经稳定优于 baseline
- `B8` 仍然是后续若要继续打磨 tail latency 的首要 canary

#### `B6-Udp`

Observed post-tuning sample:

| Metric | Post-tuning | Prior `v3` | Baseline |
|---|---:|---:|---:|
| packet loss | `0.2815%` | `0.2290%` | `0.5215%` |
| send throughput | `8.54 MB/s` | `8.55 MB/s` | `8.55 MB/s` |
| recv throughput | `8.52 MB/s` | `8.53 MB/s` | `8.50 MB/s` |

Interpretation:
- IO 面没有出现明显退化
- packet loss 仍显著优于 baseline，较首轮 `v3` 有轻微回摆

## Updated Conclusion

经过第二轮 tuning 后：

- `B14` 的 injected latency 已从首轮 `v3` 的严重回退，恢复到 baseline 同量级
- injected throughput 仍显著高于 baseline
- `B8` 的 cross-scheduler throughput 明显回收，但 latency 仍需继续观察
- `B6` 没出现明显 IO 退化

因此，当前 `v3` 的 scheduler 内核已经比首轮 `v3` 更平衡：
既保留了 coalesced wakeup 带来的吞吐优势，也通过 queue-edge wakeup 把 injected fast path 的响应性拉回来了。

## Linux Remote Validation (2026-03-13)

### Environment

- Host: `140.143.142.251`
- OS: Ubuntu 24.04
- Kernel: `6.8.0-51-generic`
- Compiler: `g++ 13.3.0`
- CMake: `3.28.3`
- Backends: `epoll`, `io_uring`

### Correctness

- `v3` `epoll`: clean build + full test matrix passed on remote
- `v3` `io_uring`: clean build + full test matrix passed on remote
- test matrix runner 已补齐 `T52`–`T57`，本地 `kqueue`、远端 `epoll`、远端 `io_uring` 三套都重新执行过一轮
- `v3` `io_uring` now includes `T57-io_uring_timeout_close_lifetime`
  - 修复前：`T57` 通过执行同目录 `B5-UdpClient` 稳定捕获 `SIGSEGV`
  - 修复后：`T57` PASS，`B5-UdpClient` 单独执行也能稳定退出

### Linux `epoll` Benchmark Comparison

> 口径说明：
> - `B2/B3` 与 `B11/B12` 取 client 侧最终平均 QPS / Throughput
> - `B4/B5` 的 server 侧样本几乎没有形成有效 echo，故以 `B5` 发送侧结果记录，真正的 UDP 对比以 `B6` 为准
> - `B7` 在 `epoll/AIO` 下两边都表现为长时间 stall，结果只说明“此路径当前不适合作为优化收益观察点”

| Benchmark | Baseline | V3 | Delta | Notes |
|---|---:|---:|---:|---|
| `B1-ComputeScheduler` | `65.36k task/s`, `160.90us`, `14.35k/s` | `70.42k task/s`, `150.10us`, `15.42k/s` | throughput `+7.7%`, latency `-6.7%`, sustained `+7.4%` | compute path 小幅变好 |
| `B2/B3-Tcp` | `56.07k QPS`, `27.38 MB/s` | `54.64k QPS`, `26.68 MB/s` | `-2.6%` | plain TCP 略回退 |
| `B4/B5-Udp` | `1249.78 pkt/s`, `0.305 MB/s` | `1785.71 pkt/s`, `0.436 MB/s` | `+42.9%` | 仅反映 client 发送侧；server 采样几乎无效 |
| `B6-Udp` | loss `0.464%`, recv `8.525 MB/s` | loss `1.102%`, recv `8.472 MB/s` | loss worsened, recv `-0.6%` | `epoll` 下 UDP 面略退 |
| `B7-FileIo` | `60.049s`, write `0.00026 MB/s` | `60.046s`, write `0.00026 MB/s` | roughly equal | 两边都 stall，当前不具备参考价值 |
| `B8-MpscChannel` | latency `2587.87us`, cross `11.49M/s`, sustained `11.78M/s` | latency `9100.01us`, cross `10.99M/s`, sustained `11.64M/s` | latency worsened, cross `-4.4%`, sustained `-1.1%` | channel latency 明显回退 |
| `B9-UnsafeChannel` | latency `0.04595us`, mpsc `15.63M/s`, speedup `4.92x` | latency `0.04105us`, mpsc `15.87M/s`, speedup `5.17x` | latency better, throughput `+1.6%` | 同 scheduler 极轻路径略好 |
| `B10-Ringbuffer` | `20.83ns/pair`, recv `36.49 GB/s`, send `16.84 GB/s` | `19.71ns/pair`, recv `38.71 GB/s`, send `18.47 GB/s` | pair `-5.4%`, recv `+6.1%`, send `+9.7%` | ringbuffer micro-bench 更好 |
| `B11/B12-TcpIov` | `50.74k QPS`, `24.78 MB/s` | `50.84k QPS`, `24.82 MB/s` | `+0.2%` | 基本持平 |
| `B13-Sendfile` | `917.43 MB/s` (`100MB`) | `900.90 MB/s` (`100MB`) | `-1.8%` | 大文件 sendfile 略回退 |
| `B14-SchedulerInjectedWakeup` | `0.84M task/s`, `7.60us` | `1.69M task/s`, `105.26us` | throughput `+101.7%`, latency worsened | `epoll` 仍是明显吞吐换延迟 |

### Linux `io_uring` Benchmark Comparison

> 口径与 `epoll` 部分一致；`B5` 在 `v3` 修复前会因 `io_uring user_data` 生命周期问题崩溃，现已可稳定完成。

| Benchmark | Baseline | V3 | Delta | Notes |
|---|---:|---:|---:|---|
| `B1-ComputeScheduler` | `68.97k task/s`, `266.11us`, `14.49k/s` | `49.26k task/s`, `1790.63us`, `14.18k/s` | throughput `-28.6%`, latency worsened, sustained `-2.1%` | `io_uring` 下 compute 调度明显退化 |
| `B2/B3-Tcp` | `72.01k QPS`, `35.16 MB/s` | `69.19k QPS`, `33.79 MB/s` | `-3.9%` | plain TCP 略退 |
| `B4/B5-Udp` | `1606.28 pkt/s`, `0.392 MB/s` | `1785.40 pkt/s`, `0.436 MB/s` | `+11.2%` | 修复后 benchmark 可稳定跑完 |
| `B6-Udp` | loss `0.7785%`, recv `8.498 MB/s` | loss `0.55%`, recv `8.518 MB/s` | loss improved, recv `+0.2%` | UDP 面有正收益 |
| `B7-FileIo` | read/write `156.25 MB/s` | read/write `156.25 MB/s` | `0.0%` | file IO 持平 |
| `B8-MpscChannel` | latency `5825.51us`, cross `10.64M/s`, sustained `9.01M/s` | latency `3960.89us`, cross `6.17M/s`, sustained `8.11M/s` | latency `-32.0%`, cross `-42.0%`, sustained `-10.0%` | tail latency 改善，但 cross-scheduler 明显变差 |
| `B9-UnsafeChannel` | latency `0.04236us`, mpsc `16.13M/s`, speedup `5.82x` | latency `0.04648us`, mpsc `15.15M/s`, speedup `4.06x` | throughput `-3.0%`, latency worsened | 极轻量 channel 回退 |
| `B10-Ringbuffer` | `19.55ns/pair`, recv `39.67 GB/s`, send `18.66 GB/s` | `20.72ns/pair`, recv `35.95 GB/s`, send `16.94 GB/s` | pair `+6.0%`, recv `-9.4%`, send `-9.2%` | ringbuffer micro-bench 回退 |
| `B11/B12-TcpIov` | `68.19k QPS`, `33.29 MB/s` | `66.99k QPS`, `32.71 MB/s` | `-1.8%` | IOV TCP 略退 |
| `B13-Sendfile` | `909.09 MB/s` (`100MB`) | `909.09 MB/s` (`100MB`) | `0.0%` | sendfile 持平 |
| `B14-SchedulerInjectedWakeup` | `0.97M task/s`, `229.63us` | `1.83M task/s`, `173.64us` | throughput `+89.9%`, latency `-24.4%` | injected path 吞吐和平均延迟都更好 |

### Linux Summary

#### `epoll`

- `B14` 依然体现出 `v3` 的核心收益：remote injected throughput 明显翻倍
- 但 `B8` latency 与 `B14` latency 说明 `epoll` 仍然偏吞吐取向
- `B1`、`B10` 这类调度/内存结构 micro-bench 有小幅收益
- 网络 IO (`B2/B3`, `B6`, `B11/B12`) 整体没有形成一致性大提升

#### `io_uring`

- 先解决了 correctness blocker：`B5` 崩溃和 `T57` 生命周期回归已经消失
- `B14` 在 `io_uring` 下也延续了 injected path 的正收益：吞吐更高，平均延迟也更低
- 但 `B1`、`B8` cross-scheduler、`B10`、`B11/B12` 显示当前 `v3` 在 `io_uring` 上还有明显性能债
- 因此 `io_uring` 目前是“稳定性补齐 + injected fast path 变好”，但不是“全局性能全面胜过 baseline”
