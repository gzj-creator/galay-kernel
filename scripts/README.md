# 脚本入口

`scripts/` 目录只保留当前正式使用的 benchmark、测试与结果解析脚本。

## 正式入口

- `run_test_matrix.sh`
  - 作用：运行构建目录中的测试矩阵，并把日志写入指定目录
  - 典型场景：全量测试验证、fresh build 验证

- `run_benchmark_matrix.sh`
  - 作用：对单个 build 目录执行一轮 benchmark matrix
  - 典型场景：同一代码版本下采集 benchmark 日志

- `run_benchmark_triplet.sh`
  - 作用：按单个后端组织 baseline 与 refactored 的对照 benchmark
  - 典型场景：kqueue / epoll / io_uring 单后端前后对照

- `run_single_benchmark_triplet.sh`
  - 作用：对单个 benchmark 依次跑 `kqueue`、`epoll`、`io_uring`
  - 典型场景：逐项排查单个 benchmark 的三端差异

- `parse_benchmark_triplet.py`
  - 作用：解析 triplet 输出目录，生成统一结果汇总
  - 典型场景：生成文档同步所需的对比矩阵

## 内部辅助脚本

- `benchmark_timeout.sh`
  - 作用：为 benchmark 运行提供超时守护
  - 说明：由 benchmark runner 调用，不建议单独作为对外入口

## 测试

脚本相关回归测试位于：

- `scripts/tests/test_run_test_matrix.py`
- `scripts/tests/test_run_benchmark_matrix.py`
- `scripts/tests/test_run_benchmark_triplet.py`
- `scripts/tests/test_run_single_benchmark_triplet.py`
- `scripts/tests/test_parse_benchmark_triplet.py`

## 约定

- 新增脚本统一使用小写下划线风格
- 已废弃的旧式编号脚本不再保留兼容别名
- 文档中引用脚本时，优先引用本页列出的正式入口
