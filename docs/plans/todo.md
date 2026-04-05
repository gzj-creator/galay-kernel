# Runtime Optimization Todo

## syscall 效率

- [x] 1. io_uring multishot accept
  - 当前每次 accept 完都要重新提交 SQE。用 `IORING_ACCEPT_MULTISHOT` 一次注册，内核持续产出 CQE，省掉反复提交的开销。这对高连接率场景收益最大。
- [x] 2. io_uring multishot recv
  - 配合 provided buffer ring（`IORING_REGISTER_PBUF_RING`），内核收到数据时自动从 buffer pool 取 buffer 填充，不需要每次 recv 前都提交一个带 buffer 指针的 SQE。
- [x] 3. io_uring registered files / registered buffers
  - 本轮先完成 `registered buffers` 侧的 provided buffer ring；`plain recv` 已切到 reactor 内部 staging + buffer select。`fixed files` 仍可继续评估，但不再阻塞当前 recv 路径优化闭环。
- [x] 4. epoll 注册批量化
  - 当前 `addRecv/addSend` 每次立即调 `epoll_ctl`。改成 lazy 模式：把变更推入 pending list，在 `poll()` 之前一次性 flush。
- [x] 5. kqueue 唤醒机制
  - 当前用 pipe。换成 `EVFILT_USER`，省掉 pipe 的两个 fd 和 `read/write` 系统调用。

## 内存分配

- [x] 6. 消除每次 IO 操作都 `new` 一个 `SqeRequestToken`
  - `IOController` 同一时间每个方向（READ/WRITE）只有一个活跃操作，使用稳定地址 token 池复用，消除热路径堆分配。
- [x] 7. 协程创建三次堆分配合并
  - 目前包含 `coroutine frame + TaskState + shared_ptr`。可合并 `TaskState` 和 `TaskCompletionState`；对不需要 `JoinHandle::join()` 的协程，避免 `mutex + condition_variable` 常驻成本。
- [x] 8. TaskState 对象池
  - `TaskState` 是 `alignas(64)` 的固定大小结构，适合做 per-thread slab allocator。高频创建销毁协程时可明显降低分配成本。
- [x] 9. IOController 中 `shared_ptr` 精简
  - `SqeState` 的生命周期当前完全受 `IOController` 管控，可评估替换为 `unique_ptr` 或直接内嵌，减少原子引用计数。

## 网络层

- [x] 10. SO_REUSEPORT 多 acceptor
  - 当前只有一个 scheduler 跑 accept，其它 scheduler 等注入。用 `SO_REUSEPORT` 让每个 IO 线程都 `bind+listen` 同一端口，由内核做连接分发。
- [x] 11. TCP_DEFER_ACCEPT
  - 设置后内核只在收到第一个数据包时才唤醒 accept，减少空连接上下文切换。
- [x] 12. accept4 替代 accept + fcntl
  - 直接在 accept 时申请 `SOCK_NONBLOCK | SOCK_CLOEXEC`，省掉额外 `fcntl` 调用。
- [x] 13. io_uring zero-copy send（`IORING_OP_SEND_ZC`）
  - 对大包发送场景避免内核侧 buffer copy。需要 kernel 6.0+。

## 优先级排序

- 第一批：`6`、`12`、`5`
- 第二批：`1`、`2`、`3`
- 第三批：`7`、`8`、`10`
- 第四批：`4`、`13`、`11`

## 已完成项验证锚点

- `1`：`test/T107-io_uring_multishot_accept_source_case.cc`、`test/T108-io_uring_multishot_accept_runtime.cc`
- `4`：`test/T106-epoll_lazy_registration_source_case.cc`
- `5`：`test/T39-scheduler_wakeup_coalescing.cc`、`test/T42-scheduler_queue_edge_wakeup.cc`、`test/T105-runtime_fastpath_source_case.cc`
- `6`：`test/T105-runtime_fastpath_source_case.cc`
- `7`：`test/T48-runtime_spawn_join_handle.cc`、`test/T50-runtime_spawn_blocking.cc`、`test/T52-runtime_task_api_surface.cc`、`test/T115-runtime_task_storage_source_case.cc`
- `8`：`test/T22-runtime_stress.cc`、`test/T100-chase_lev_ring_steal_pop_stress.cc`、`test/T115-runtime_task_storage_source_case.cc`
- `9`：`test/T95-io_controller_move_only_surface.cc`、`test/T116-io_controller_sqe_state_source_case.cc`、`test/T108-io_uring_multishot_accept_runtime.cc`、`test/T111-io_uring_multishot_recv_runtime.cc`
- `10`：`test/T112-runtime_reuseport_source_case.cc`、`benchmark/B2-tcp_server.cc`、`benchmark/B11-tcp_iov_server.cc`
- `11`：`test/T109-tcp_defer_accept_source_case.cc`
- `12`：`test/T105-runtime_fastpath_source_case.cc`
- `2`：`test/T110-io_uring_multishot_recv_source_case.cc`、`test/T111-io_uring_multishot_recv_runtime.cc`
- `3`：`test/T110-io_uring_multishot_recv_source_case.cc`、`test/T111-io_uring_multishot_recv_runtime.cc`
- `13`：`test/T113-io_uring_send_zc_source_case.cc`、`test/T114-io_uring_send_zc_runtime.cc`
