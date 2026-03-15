# Single Benchmark Triplet Results

## 2026-03-15 Scope

- Comparison contract: `baseline(cde3da1) -> refactored(current .worktrees/v3)`
- Smoke target: `B5-UdpClient` only, used for stability/hang/crash checking
- Final targets: `B1`, `B6`, `B14`, `B8`, `B9`, `B10`, `B3`, `B7`, `B12`, `B13`
- Final repeat count: `3`
- Backend order: `kqueue -> epoll -> io_uring`
- Timeout guard: `scripts/benchmark_timeout.sh` via `--timeout-seconds` in triplet runners

## B5-UdpClient Smoke

### Output roots

- `kqueue`: `/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/final-triplet-2026-03-15/kqueue-smoke/B5-UdpClient`
- `epoll`: `/home/ubuntu/galay-kernel-triplet-base/build/final-triplet-2026-03-15/epoll-smoke/B5-UdpClient`
- `io_uring`: `/home/ubuntu/galay-kernel-triplet-base/build/final-triplet-2026-03-15/io_uring-smoke/B5-UdpClient`

### Smoke matrix

| Backend | packet loss | recv throughput | send throughput | Status |
| --- | ---: | ---: | ---: | --- |
| `kqueue` | baseline `100.00%` -> refactored `100.00%` | baseline `0.00 MB/s` -> refactored `0.00 MB/s` | baseline `0.30 MB/s` -> refactored `0.44 MB/s` | `pass` |
| `epoll` | baseline `100.00%` -> refactored `100.00%` | baseline `0.00 MB/s` -> refactored `0.00 MB/s` | baseline `0.31 MB/s` -> refactored `0.44 MB/s` | `pass` |
| `io_uring` | baseline `unsupported` -> refactored `100.00%` | baseline `unsupported` -> refactored `0.00 MB/s` | baseline `unsupported` -> refactored `0.44 MB/s` | `unsupported` |

### Smoke notes

- `B5` remains smoke-only; final UDP performance sign-off uses `B6-Udp`.
- `io_uring` baseline `B5` stays historical-path unsupported by design.

## Final Repeat=3 Matrix

### Output roots

- `kqueue`: `/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/final-triplet-2026-03-15/kqueue-final`
- `epoll`: `/home/ubuntu/galay-kernel-triplet-base/build/final-triplet-2026-03-15/epoll-final`
- `io_uring`: `/home/ubuntu/galay-kernel-triplet-base/build/final-triplet-2026-03-15/io_uring-final`

### Backend status snapshot

| Backend | PASS rows | FAIL rows | UNSUPPORTED rows |
| --- | ---: | ---: | ---: |
| `kqueue` | 40 | 0 | 0 |
| `epoll` | 39 | 1 | 0 |
| `io_uring` | 34 | 3 | 3 |

### Non-pass rows

| Backend | Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `epoll` | `B8-MpscChannel` | single producer throughput | 6.37M msg/s | 5.85M msg/s | -8.2% | `fail` |
| `io_uring` | `B6-Udp` | packet loss | unsupported | 0.62% | n/a | `unsupported` |
| `io_uring` | `B6-Udp` | recv throughput | unsupported | 8.82 MB/s | n/a | `unsupported` |
| `io_uring` | `B6-Udp` | send throughput | unsupported | 8.88 MB/s | n/a | `unsupported` |
| `io_uring` | `B8-MpscChannel` | single producer throughput | 7.53M msg/s | 6.78M msg/s | -9.9% | `fail` |
| `io_uring` | `B12-TcpIovClient` | average qps | 115.05K qps | 98.72K qps | -14.2% | `fail` |
| `io_uring` | `B12-TcpIovClient` | average throughput | 56.18 MB/s | 48.20 MB/s | -14.2% | `fail` |

### kqueue

| Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |
| --- | --- | ---: | ---: | ---: | --- |
| `B1-ComputeScheduler` | empty throughput | 1.76M task/s | 1.77M task/s | +0.7% | `pass` |
| `B1-ComputeScheduler` | heavy throughput | 502.13K task/s | 495.82K task/s | -1.3% | `pass` |
| `B1-ComputeScheduler` | latency | 2.62 us | 2.44 us | -6.6% | `pass` |
| `B1-ComputeScheduler` | light throughput | 1.81M task/s | 1.82M task/s | +0.4% | `pass` |
| `B6-Udp` | packet loss | 0.15% | 0.00% | -100.0% | `pass` |
| `B6-Udp` | recv throughput | 8.85 MB/s | 8.86 MB/s | +0.2% | `pass` |
| `B6-Udp` | send throughput | 8.86 MB/s | 8.86 MB/s | +0.0% | `pass` |
| `B14-SchedulerInjectedWakeup` | injected latency | 2.95 us | 2.84 us | -3.8% | `pass` |
| `B14-SchedulerInjectedWakeup` | injected throughput | 1.22M task/s | 4.11M task/s | +237.6% | `pass` |
| `B8-MpscChannel` | batch throughput | 18.01M msg/s | 18.14M msg/s | +0.7% | `pass` |
| `B8-MpscChannel` | cross-scheduler throughput | 13.81M msg/s | 18.14M msg/s | +31.3% | `pass` |
| `B8-MpscChannel` | latency | 215.41 us | 2.95 us | -98.6% | `pass` |
| `B8-MpscChannel` | multi producer throughput | 16.44M msg/s | 20.46M msg/s | +24.5% | `pass` |
| `B8-MpscChannel` | single producer throughput | 14.91M msg/s | 19.06M msg/s | +27.8% | `pass` |
| `B8-MpscChannel` | sustained throughput | 11.42M msg/s | 10.70M msg/s | -6.3% | `pass` |
| `B9-UnsafeChannel` | batch throughput | 43.48M msg/s | 51.37M msg/s | +18.1% | `pass` |
| `B9-UnsafeChannel` | latency | 0.02 us | 0.02 us | -4.0% | `pass` |
| `B9-UnsafeChannel` | mpsc reference throughput | 76.92M msg/s | 126.31M msg/s | +64.2% | `pass` |
| `B9-UnsafeChannel` | recvBatched throughput | 250.00M msg/s | 298.42M msg/s | +19.4% | `pass` |
| `B9-UnsafeChannel` | unsafe throughput | 142.86M msg/s | 175.13M msg/s | +22.6% | `pass` |
| `B10-Ringbuffer` | network receive throughput | 77881.60 MB/s | 72275.20 MB/s | -7.2% | `pass` |
| `B10-Ringbuffer` | network send throughput | 53214.10 MB/s | 53356.10 MB/s | +0.3% | `pass` |
| `B10-Ringbuffer` | produce+consume latency | 7.76 ns/pair | 7.77 ns/pair | +0.2% | `pass` |
| `B10-Ringbuffer` | read iovecs latency | 1.02 ns/call | 1.02 ns/call | +0.2% | `pass` |
| `B10-Ringbuffer` | read/write throughput | 50520.50 MB/s | 48650.70 MB/s | -3.7% | `pass` |
| `B10-Ringbuffer` | wrap around throughput | 14442.00 MB/s | 14467.40 MB/s | +0.2% | `pass` |
| `B10-Ringbuffer` | write iovecs latency | 1.77 ns/call | 1.90 ns/call | +7.1% | `pass` |
| `B10-Ringbuffer` | write throughput | 70401.20 MB/s | 72343.10 MB/s | +2.8% | `pass` |
| `B3-TcpClient` | average qps | 289.55K qps | 290.12K qps | +0.2% | `pass` |
| `B3-TcpClient` | average throughput | 141.38 MB/s | 141.66 MB/s | +0.2% | `pass` |
| `B7-FileIo` | read iops | 37.04K iops | 37.74K iops | +1.9% | `pass` |
| `B7-FileIo` | read throughput | 144.68 MB/s | 147.41 MB/s | +1.9% | `pass` |
| `B7-FileIo` | write iops | 37.04K iops | 37.74K iops | +1.9% | `pass` |
| `B7-FileIo` | write throughput | 144.68 MB/s | 147.41 MB/s | +1.9% | `pass` |
| `B12-TcpIovClient` | average qps | 280.45K qps | 283.31K qps | +1.0% | `pass` |
| `B12-TcpIovClient` | average throughput | 136.94 MB/s | 138.34 MB/s | +1.0% | `pass` |
| `B13-Sendfile` | sendfile 100MB throughput | 1086.96 MB/s | 1149.43 MB/s | +5.7% | `pass` |
| `B13-Sendfile` | sendfile 10MB throughput | 138.89 MB/s | 151.51 MB/s | +9.1% | `pass` |
| `B13-Sendfile` | sendfile 1MB throughput | 14.49 MB/s | 14.29 MB/s | -1.4% | `pass` |
| `B13-Sendfile` | sendfile 50MB throughput | 632.91 MB/s | 609.76 MB/s | -3.7% | `pass` |

### epoll

| Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |
| --- | --- | ---: | ---: | ---: | --- |
| `B1-ComputeScheduler` | empty throughput | 1.34M task/s | 1.30M task/s | -3.6% | `pass` |
| `B1-ComputeScheduler` | heavy throughput | 82.75K task/s | 81.61K task/s | -1.4% | `pass` |
| `B1-ComputeScheduler` | latency | 15.88 us | 15.73 us | -0.9% | `pass` |
| `B1-ComputeScheduler` | light throughput | 1.73M task/s | 1.97M task/s | +14.3% | `pass` |
| `B6-Udp` | packet loss | 0.27% | 0.00% | -100.0% | `pass` |
| `B6-Udp` | recv throughput | 8.85 MB/s | 8.88 MB/s | +0.3% | `pass` |
| `B6-Udp` | send throughput | 8.88 MB/s | 8.88 MB/s | +0.0% | `pass` |
| `B14-SchedulerInjectedWakeup` | injected latency | 17.35 us | 17.22 us | -0.8% | `pass` |
| `B14-SchedulerInjectedWakeup` | injected throughput | 1.39M task/s | 2.80M task/s | +101.6% | `pass` |
| `B8-MpscChannel` | batch throughput | 9.64M msg/s | 9.88M msg/s | +2.5% | `pass` |
| `B8-MpscChannel` | cross-scheduler throughput | 5.51M msg/s | 18.19M msg/s | +230.4% | `pass` |
| `B8-MpscChannel` | latency | 3467.65 us | 256.30 us | -92.6% | `pass` |
| `B8-MpscChannel` | multi producer throughput | 9.58M msg/s | 13.94M msg/s | +45.5% | `pass` |
| `B8-MpscChannel` | single producer throughput | 6.37M msg/s | 5.85M msg/s | -8.2% | `fail` |
| `B8-MpscChannel` | sustained throughput | 7.80M msg/s | 10.45M msg/s | +34.1% | `pass` |
| `B9-UnsafeChannel` | batch throughput | 29.41M msg/s | 30.15M msg/s | +2.5% | `pass` |
| `B9-UnsafeChannel` | latency | 0.04 us | 0.04 us | -0.1% | `pass` |
| `B9-UnsafeChannel` | mpsc reference throughput | 15.87M msg/s | 26.51M msg/s | +67.0% | `pass` |
| `B9-UnsafeChannel` | recvBatched throughput | 125.00M msg/s | 129.45M msg/s | +3.6% | `pass` |
| `B9-UnsafeChannel` | unsafe throughput | 90.91M msg/s | 90.71M msg/s | -0.2% | `pass` |
| `B10-Ringbuffer` | network receive throughput | 42311.90 MB/s | 41834.00 MB/s | -1.1% | `pass` |
| `B10-Ringbuffer` | network send throughput | 19168.10 MB/s | 19267.10 MB/s | +0.5% | `pass` |
| `B10-Ringbuffer` | produce+consume latency | 19.15 ns/pair | 19.10 ns/pair | -0.3% | `pass` |
| `B10-Ringbuffer` | read iovecs latency | 2.55 ns/call | 2.56 ns/call | +0.4% | `pass` |
| `B10-Ringbuffer` | read/write throughput | 28947.80 MB/s | 29046.30 MB/s | +0.3% | `pass` |
| `B10-Ringbuffer` | wrap around throughput | 4173.27 MB/s | 4251.52 MB/s | +1.9% | `pass` |
| `B10-Ringbuffer` | write iovecs latency | 3.95 ns/call | 4.24 ns/call | +7.4% | `pass` |
| `B10-Ringbuffer` | write throughput | 35417.80 MB/s | 35952.20 MB/s | +1.5% | `pass` |
| `B3-TcpClient` | average qps | 100.80K qps | 100.81K qps | +0.0% | `pass` |
| `B3-TcpClient` | average throughput | 49.22 MB/s | 49.22 MB/s | +0.0% | `pass` |
| `B7-FileIo` | read iops | 0.00 iops | 0.00 iops | n/a | `pass` |
| `B7-FileIo` | read throughput | 0.00 MB/s | 0.00 MB/s | n/a | `pass` |
| `B7-FileIo` | write iops | 0.07 iops | 0.07 iops | +0.0% | `pass` |
| `B7-FileIo` | write throughput | 0.00 MB/s | 0.00 MB/s | +0.0% | `pass` |
| `B12-TcpIovClient` | average qps | 95.70K qps | 92.72K qps | -3.1% | `pass` |
| `B12-TcpIovClient` | average throughput | 46.73 MB/s | 45.28 MB/s | -3.1% | `pass` |
| `B13-Sendfile` | sendfile 100MB throughput | 1000.00 MB/s | 1000.00 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 10MB throughput | 166.67 MB/s | 166.67 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 1MB throughput | 16.67 MB/s | 16.67 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 50MB throughput | 625.00 MB/s | 625.00 MB/s | +0.0% | `pass` |

### io_uring

| Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |
| --- | --- | ---: | ---: | ---: | --- |
| `B1-ComputeScheduler` | empty throughput | 1.33M task/s | 1.36M task/s | +2.3% | `pass` |
| `B1-ComputeScheduler` | heavy throughput | 72.23K task/s | 82.64K task/s | +14.4% | `pass` |
| `B1-ComputeScheduler` | latency | 15.78 us | 16.22 us | +2.8% | `pass` |
| `B1-ComputeScheduler` | light throughput | 1.84M task/s | 1.83M task/s | -0.4% | `pass` |
| `B6-Udp` | packet loss | unsupported | 0.62% | n/a | `unsupported` |
| `B6-Udp` | recv throughput | unsupported | 8.82 MB/s | n/a | `unsupported` |
| `B6-Udp` | send throughput | unsupported | 8.88 MB/s | n/a | `unsupported` |
| `B14-SchedulerInjectedWakeup` | injected latency | 16.26 us | 16.40 us | +0.9% | `pass` |
| `B14-SchedulerInjectedWakeup` | injected throughput | 1.42M task/s | 2.87M task/s | +101.7% | `pass` |
| `B8-MpscChannel` | batch throughput | 10.16M msg/s | 11.50M msg/s | +13.2% | `pass` |
| `B8-MpscChannel` | cross-scheduler throughput | 6.84M msg/s | 21.79M msg/s | +218.6% | `pass` |
| `B8-MpscChannel` | latency | 4766.10 us | 1231.81 us | -74.2% | `pass` |
| `B8-MpscChannel` | multi producer throughput | 10.09M msg/s | 16.09M msg/s | +59.6% | `pass` |
| `B8-MpscChannel` | single producer throughput | 7.53M msg/s | 6.78M msg/s | -9.9% | `fail` |
| `B8-MpscChannel` | sustained throughput | 7.93M msg/s | 11.35M msg/s | +43.1% | `pass` |
| `B9-UnsafeChannel` | batch throughput | 29.41M msg/s | 30.21M msg/s | +2.7% | `pass` |
| `B9-UnsafeChannel` | latency | 0.04 us | 0.04 us | -1.5% | `pass` |
| `B9-UnsafeChannel` | mpsc reference throughput | 15.87M msg/s | 26.39M msg/s | +66.3% | `pass` |
| `B9-UnsafeChannel` | recvBatched throughput | 125.00M msg/s | 130.21M msg/s | +4.2% | `pass` |
| `B9-UnsafeChannel` | unsafe throughput | 90.91M msg/s | 91.11M msg/s | +0.2% | `pass` |
| `B10-Ringbuffer` | network receive throughput | 42804.60 MB/s | 42009.70 MB/s | -1.9% | `pass` |
| `B10-Ringbuffer` | network send throughput | 19028.80 MB/s | 18646.30 MB/s | -2.0% | `pass` |
| `B10-Ringbuffer` | produce+consume latency | 19.09 ns/pair | 19.10 ns/pair | +0.0% | `pass` |
| `B10-Ringbuffer` | read iovecs latency | 2.56 ns/call | 2.29 ns/call | -10.6% | `pass` |
| `B10-Ringbuffer` | read/write throughput | 28778.60 MB/s | 31154.00 MB/s | +8.3% | `pass` |
| `B10-Ringbuffer` | wrap around throughput | 4178.86 MB/s | 4484.09 MB/s | +7.3% | `pass` |
| `B10-Ringbuffer` | write iovecs latency | 4.26 ns/call | 3.92 ns/call | -7.9% | `pass` |
| `B10-Ringbuffer` | write throughput | 35704.80 MB/s | 35700.20 MB/s | -0.0% | `pass` |
| `B3-TcpClient` | average qps | 121.82K qps | 118.80K qps | -2.5% | `pass` |
| `B3-TcpClient` | average throughput | 59.48 MB/s | 58.01 MB/s | -2.5% | `pass` |
| `B7-FileIo` | read iops | 40.00K iops | 40.00K iops | +0.0% | `pass` |
| `B7-FileIo` | read throughput | 156.25 MB/s | 156.25 MB/s | +0.0% | `pass` |
| `B7-FileIo` | write iops | 40.00K iops | 40.00K iops | +0.0% | `pass` |
| `B7-FileIo` | write throughput | 156.25 MB/s | 156.25 MB/s | +0.0% | `pass` |
| `B12-TcpIovClient` | average qps | 115.05K qps | 98.72K qps | -14.2% | `fail` |
| `B12-TcpIovClient` | average throughput | 56.18 MB/s | 48.20 MB/s | -14.2% | `fail` |
| `B13-Sendfile` | sendfile 100MB throughput | 909.09 MB/s | 909.09 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 10MB throughput | 166.67 MB/s | 166.67 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 1MB throughput | 16.67 MB/s | 16.67 MB/s | +0.0% | `pass` |
| `B13-Sendfile` | sendfile 50MB throughput | 625.00 MB/s | 625.00 MB/s | +0.0% | `pass` |

## Current readout

- `kqueue`: 40/40 rows pass, with the largest wins on `B14 injected throughput`, `B8 latency`, `B8 cross-scheduler throughput`, and the entire `B9` family.
- `epoll`: 39/40 rows pass; the only failing row is `B8 single producer throughput` at `-8.2%`, while `B8 cross-scheduler throughput` and `B14 injected throughput` both improve substantially.
- `io_uring`: 34 pass / 3 fail / 3 unsupported; unsupported rows are the quarantined historical baseline `B6-Udp`, while measured regressions are `B8 single producer throughput` and both `B12-TcpIovClient` rows.
