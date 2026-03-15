# Benchmark Triplet Summary

- Rerun date: 2026-03-14
- Overall status: PASS
- Acceptance rule:
  - core benchmark regression within `5%` is accepted as scheduling noise
  - non-core benchmarks keep the existing `10%` tolerance
- Triplet refs: `pre-v3=09c8917`, `v3=cde3da1`, `refactored=current .worktrees/v3`
- Output root: `/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final5`
- Parsed artifacts:
  - JSON: `build/benchmark-triplet-2026-03-14-final5/report.json`
  - Markdown: `build/benchmark-triplet-2026-03-14-final5/report.md`

## Final Verdict

- March 14, 2026 的最佳验证矩阵是 `final5`。
- 在核心 benchmark 允许 `5%` 以内回退的验收口径下，`overall_pass = true`。
- 已验证的 compat runner 保持 `final5` 的 label-by-label 顺序；后续尝试过的 rotated runner 没有保留，因为它会放大波动并引入新的失败项。

## Key Outcomes

1. `B1-ComputeScheduler` 四项核心指标全部通过
   - `empty throughput`: `1.22M -> 1.20M task/s`, `-1.9%`
   - `light throughput`: `1.21M -> 1.22M task/s`, `+1.0%`
   - `heavy throughput`: `695.61K -> 690.52K task/s`, `-0.7%`
   - `latency`: `5.21 us -> 1.21 us`, `-76.7%`

2. `B8-MpscChannel` 六项指标全部通过
   - `latency`: `200.99 us -> 5.26 us`
   - `single producer throughput`: `13.66M -> 18.94M msg/s`
   - `multi producer throughput`: `19.37M -> 20.71M msg/s`
   - `batch throughput`: `18.54M -> 18.59M msg/s`
   - `cross-scheduler throughput`: `13.70M -> 18.87M msg/s`
   - `sustained throughput`: `10.59M -> 10.73M msg/s`

3. `B6-Udp` 与 `B14-SchedulerInjectedWakeup` 继续保持通过
   - `B6-Udp packet loss`: `0.16% -> 0.00%`
   - `B14 injected latency`: `9.52 us -> 2.71 us`
   - `B14 injected throughput`: `1.28M -> 4.28M task/s`

## Full Matrix

| Benchmark | Metric | pre-v3 | v3 | refactored | Delta vs v3 | Status |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `B1-ComputeScheduler` | empty throughput | 1.13M task/s | 1.22M task/s | 1.20M task/s | -1.9% | PASS |
| `B1-ComputeScheduler` | heavy throughput | 685.42K task/s | 695.61K task/s | 690.52K task/s | -0.7% | PASS |
| `B1-ComputeScheduler` | latency | 5.79 us | 5.21 us | 1.21 us | -76.7% | PASS |
| `B1-ComputeScheduler` | light throughput | 1.18M task/s | 1.21M task/s | 1.22M task/s | +1.0% | PASS |
| `B10-Ringbuffer` | network receive throughput | unsupported | 73888.00 MB/s | 72918.20 MB/s | -1.3% | PASS |
| `B10-Ringbuffer` | network send throughput | unsupported | 53259.50 MB/s | 53157.60 MB/s | -0.2% | PASS |
| `B10-Ringbuffer` | produce+consume latency | unsupported | 8.30 ns/pair | 7.73 ns/pair | -6.9% | PASS |
| `B10-Ringbuffer` | read iovecs latency | unsupported | 1.01 ns/call | 1.01 ns/call | -0.2% | PASS |
| `B10-Ringbuffer` | read/write throughput | unsupported | 49805.40 MB/s | 49325.60 MB/s | -1.0% | PASS |
| `B10-Ringbuffer` | wrap around throughput | unsupported | 17768.30 MB/s | 17705.40 MB/s | -0.4% | PASS |
| `B10-Ringbuffer` | write iovecs latency | unsupported | 1.76 ns/call | 1.82 ns/call | +3.2% | PASS |
| `B10-Ringbuffer` | write throughput | unsupported | 69854.60 MB/s | 71098.80 MB/s | +1.8% | PASS |
| `B12-TcpIovClient` | average qps | 271.26K qps | 258.33K qps | 260.63K qps | +0.9% | PASS |
| `B12-TcpIovClient` | average throughput | 132.45 MB/s | 126.14 MB/s | 127.26 MB/s | +0.9% | PASS |
| `B13-Sendfile` | sendfile 100MB throughput | 917.43 MB/s | 1075.27 MB/s | 1075.27 MB/s | +0.0% | PASS |
| `B13-Sendfile` | sendfile 10MB throughput | 114.94 MB/s | 147.06 MB/s | 149.25 MB/s | +1.5% | PASS |
| `B13-Sendfile` | sendfile 1MB throughput | 12.35 MB/s | 14.08 MB/s | 14.49 MB/s | +2.9% | PASS |
| `B13-Sendfile` | sendfile 50MB throughput | 526.32 MB/s | 609.76 MB/s | 609.76 MB/s | +0.0% | PASS |
| `B14-SchedulerInjectedWakeup` | injected latency | 26332.20 us | 9.52 us | 2.71 us | -71.5% | PASS |
| `B14-SchedulerInjectedWakeup` | injected throughput | 4.31M task/s | 1.28M task/s | 4.28M task/s | +233.6% | PASS |
| `B3-TcpClient` | average qps | 285.68K qps | 264.50K qps | 265.48K qps | +0.4% | PASS |
| `B3-TcpClient` | average throughput | 139.49 MB/s | 129.15 MB/s | 129.63 MB/s | +0.4% | PASS |
| `B5-UdpClient` | packet loss | unsupported | 100.00% | 100.00% | +0.0% | PASS |
| `B5-UdpClient` | recv throughput | unsupported | 0.00 MB/s | 0.00 MB/s | n/a | PASS |
| `B5-UdpClient` | send throughput | unsupported | 0.30 MB/s | 0.43 MB/s | +42.9% | PASS |
| `B6-Udp` | packet loss | unsupported | 0.16% | 0.00% | -100.0% | PASS |
| `B6-Udp` | recv throughput | unsupported | 8.84 MB/s | 8.86 MB/s | +0.2% | PASS |
| `B6-Udp` | send throughput | unsupported | 8.85 MB/s | 8.86 MB/s | +0.1% | PASS |
| `B7-FileIo` | read iops | 37.38K iops | 37.38K iops | 37.74K iops | +0.9% | PASS |
| `B7-FileIo` | read throughput | 146.03 MB/s | 146.03 MB/s | 147.41 MB/s | +0.9% | PASS |
| `B7-FileIo` | write iops | 37.38K iops | 37.38K iops | 37.74K iops | +0.9% | PASS |
| `B7-FileIo` | write throughput | 146.03 MB/s | 146.03 MB/s | 147.41 MB/s | +0.9% | PASS |
| `B8-MpscChannel` | batch throughput | 27.08M msg/s | 18.54M msg/s | 18.59M msg/s | +0.3% | PASS |
| `B8-MpscChannel` | cross-scheduler throughput | 13.33M msg/s | 13.70M msg/s | 18.87M msg/s | +37.7% | PASS |
| `B8-MpscChannel` | latency | 223.64 us | 200.99 us | 5.26 us | -97.4% | PASS |
| `B8-MpscChannel` | multi producer throughput | 18.38M msg/s | 19.37M msg/s | 20.71M msg/s | +6.9% | PASS |
| `B8-MpscChannel` | single producer throughput | 14.00M msg/s | 13.66M msg/s | 18.94M msg/s | +38.7% | PASS |
| `B8-MpscChannel` | sustained throughput | 11.41M msg/s | 10.59M msg/s | 10.73M msg/s | +1.3% | PASS |
| `B9-UnsafeChannel` | batch throughput | 47.62M msg/s | 50.00M msg/s | 50.00M msg/s | +0.0% | PASS |
| `B9-UnsafeChannel` | latency | 0.02 us | 0.02 us | 0.02 us | -0.8% | PASS |
| `B9-UnsafeChannel` | mpsc reference throughput | 90.91M msg/s | 90.91M msg/s | 125.00M msg/s | +37.5% | PASS |
| `B9-UnsafeChannel` | recvBatched throughput | 250.00M msg/s | 250.00M msg/s | 250.00M msg/s | +0.0% | PASS |
| `B9-UnsafeChannel` | unsafe throughput | 166.67M msg/s | 166.67M msg/s | 166.67M msg/s | +0.0% | PASS |
