# Test Suite Cleanup Design

**Scope**
- 清理 `test/` 中低价值 benchmark/build 校验测试。
- 统一剩余 `T*.cc` 为连续编号。
- 同步更新 `docs/` 与 `docs/plans/` 中的测试文件名与 target 引用。

**Non-Goals**
- 不重写功能测试内容。
- 不改动运行时、调度器、网络 IO 实现。
- 不改变 `test/CMakeLists.txt` 的按文件自动注册模式。

**Deletion Policy**
- 删除只服务于 benchmark 内部辅助或源码扫描的测试：
  - `T51-cmake_source_case.cc`
  - `T59-benchmark_sync_wait_ready.cc`
  - `T60-benchmark_completion_latch.cc`
  - `T65-benchmark_start_gate.cc`
  - `T66-benchmark_median_element.cc`
  - `T67-benchmark_default_scheduler_count.cc`
  - `T68-b8_cross_scheduler_source_case.cc`
  - `T69-b8_producer_throughput_source_case.cc`
  - `T70-b1_throughput_sample_source_case.cc`
  - `T71-b8_batch_sample_duration_source_case.cc`
  - `T72-b8_single_producer_gate_source_case.cc`
  - `T73-b8_single_sample_duration_source_case.cc`
  - `T74-b9_throughput_precision_source_case.cc`
  - `T75-b9_throughput_sampling_source_case.cc`

**Renumbering Policy**
- 以现有数字顺序为主序，重复号按文件名字典序稳定排序。
- 删除 14 个低价值测试后，剩余 111 个测试统一编号为 `T1..T111`。
- 文件名、文件头注释、运行输出中的 `Txx` 标识保持一致。

**Docs Alignment**
- 更新 `docs/` 与 `docs/plans/` 内所有仍然存在的测试文件名和 target 名称。
- 删除测试的文档引用直接移除；如果是计划文档中的历史验证锚点，则保留语义描述但去掉失效文件名。

**Verification**
- 重新配置并构建本地 `kqueue` 测试树。
- 运行一组受重编号影响且覆盖公共路径的定向 `ctest`。
- 用搜索确认不存在失效的 `test/T*.cc` 或旧 target 引用。
