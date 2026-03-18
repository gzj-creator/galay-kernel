# Kernel v3.2.0 Galay Multi-Repo Rollout Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 让所有实际发生改动的 `galay-*` 仓库完成 `galay-kernel v3.2.0` 的 Awaitable 适配、fresh 回归、fresh 压测、文档同步、中文提交、大版本 tag 与 GitHub release。

**Architecture:** 总控仓放在 `galay-kernel` 的独立 worktree；以 `galay-ssl` 当前实现为迁移基线，按波次推进。第一波先改 `galay-http` 与 `galay-rpc`，第二波并行改 `galay-mysql`、`galay-mongo`、`galay-redis`，第三波只对 `galay-etcd`、`galay-mcp` 做兼容验证和必要时的最小修补。每个实际改动仓库都必须先 red-green 完成新增回归，再进入压测、文档、版本和发布步骤。

**Tech Stack:** C++23、CMake、git worktree、gh CLI、OpenSSL、MySQL、Redis、MongoDB、etcd、bash。

---

### Task 1: 建立总台账与各仓库独立 worktree

**Files:**
- Create: `/Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/baseline.md`

**Step 1: 创建结果根目录**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18
```

Expected: 目录创建成功。

**Step 2: 写基线台账**

在 `/Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/baseline.md` 记录以下仓库的当前分支、最新 tag、是否有脏工作区：

- `/Users/gongzhijie/Desktop/projects/git/galay-ssl`
- `/Users/gongzhijie/Desktop/projects/git/galay-http`
- `/Users/gongzhijie/Desktop/projects/git/galay-rpc`
- `/Users/gongzhijie/Desktop/projects/git/galay-mysql`
- `/Users/gongzhijie/Desktop/projects/git/galay-mongo`
- `/Users/gongzhijie/Desktop/projects/git/galay-redis`
- `/Users/gongzhijie/Desktop/projects/git/galay-etcd`
- `/Users/gongzhijie/Desktop/projects/git/galay-mcp`

**Step 3: 为每个仓库创建独立 worktree**

统一分支名：`kernel-v320-adapt`

推荐位置：

- 项目内已有 `.worktrees` 的仓库：
  - `/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/`
- 其余仓库：
  - `~/.config/superpowers/worktrees/<repo>/kernel-v320-adapt`

Run:

```bash
git -C /Users/gongzhijie/Desktop/projects/git/galay-http worktree add /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt -b kernel-v320-adapt
git -C /Users/gongzhijie/Desktop/projects/git/galay-rpc worktree add /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-ssl
git -C /Users/gongzhijie/Desktop/projects/git/galay-ssl worktree add ~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-mysql
git -C /Users/gongzhijie/Desktop/projects/git/galay-mysql worktree add ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-mongo
git -C /Users/gongzhijie/Desktop/projects/git/galay-mongo worktree add ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-redis
git -C /Users/gongzhijie/Desktop/projects/git/galay-redis worktree add ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-etcd
git -C /Users/gongzhijie/Desktop/projects/git/galay-etcd worktree add ~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt -b kernel-v320-adapt

mkdir -p ~/.config/superpowers/worktrees/galay-mcp
git -C /Users/gongzhijie/Desktop/projects/git/galay-mcp worktree add ~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt -b kernel-v320-adapt
```

Expected: 所有 worktree 创建成功。

**Step 4: 验证 worktree 状态**

Run:

```bash
for path in \
  /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt \
  /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt \
  ~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt; do
  git -C "$path" status --short --branch
done
```

Expected: 每个 worktree 都在 `kernel-v320-adapt` 分支且工作区干净。

### Task 2: 对 `galay-ssl` 做样板复核与基准留档

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/05-性能测试.md`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/releases/v2.0.0.md`

**Step 1: 先跑 `galay-ssl` 的现有状态机测试**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-ssl -B ~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON -DBUILD_EXAMPLES=ON
cmake --build ~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt/build-v320 --target \
  T1-SslSocketTest T2-SslLoopbackSmoke T3-SslSingleShotSemantics T4-SslStateMachineSurface \
  T5-SslRecvSendStateMachine T6-SslCustomStateMachine T7-SslBuilderSurface T8-SslBuilderProtocol --parallel
for name in T1-SslSocketTest T2-SslLoopbackSmoke T3-SslSingleShotSemantics T4-SslStateMachineSurface T5-SslRecvSendStateMachine T6-SslCustomStateMachine T7-SslBuilderSurface T8-SslBuilderProtocol; do
  ~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt/build-v320/bin/$name
done
```

Expected: 全部退出 `0`。

**Step 2: 跑 SSL 压测样板**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-ssl
~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt/build-v320/bin/B1-SslBenchServer 8443 \
  /Users/gongzhijie/Desktop/projects/git/galay-ssl/certs/server.crt \
  /Users/gongzhijie/Desktop/projects/git/galay-ssl/certs/server.key \
  >/tmp/galay-ssl-bench-server.log 2>&1 &
SERVER_PID=$!
~/.config/superpowers/worktrees/galay-ssl/kernel-v320-adapt/build-v320/bin/B1-SslBenchClient 127.0.0.1 8443 200 500 47 4 \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-ssl/B1-SslBenchClient.txt
kill ${SERVER_PID}
```

Expected: client 退出 `0`，无 crash / hang。

**Step 3: 更新文档为迁移基线**

在以下文件补充“这是 `v3.2.0` 迁移参考实现”的说明：

- `/Users/gongzhijie/Desktop/projects/git/galay-ssl/README.md`
- `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/02-API参考.md`
- `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/05-性能测试.md`

**Step 4: 写 release notes 草稿**

在 `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/releases/v2.0.0.md` 记录：

- 新 Awaitable / builder 模型
- 完整回归命令
- 压测命令与结果摘要

### Task 3: 第一波迁移 `galay-http`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/SslRecvCompatAwaitable.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http/HttpSession.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http/HttpReader.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http/HttpWriter.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http2/H2Client.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http2/H2cClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/http2/Http2Conn.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/websocket/WsClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/websocket/WsReader.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/websocket/WsWriter.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/kernel/websocket/WsSession.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/test/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-http/test/T57-awaitable_v320_surface.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/docs/03-使用指南.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/galay-http/protoc/http/HttpBase.h`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-http/docs/releases/v2.0.0.md`

**Step 1: 写 failing surface test**

新增 `T57-awaitable_v320_surface.cc`，至少覆盖：

- HTTP/1.1 client `send -> recv`
- WebSocket upgrade `send -> recv`
- H2C / H2 至少一条顺序读写链路

**Step 2: 运行测试确认先失败**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-http -B /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON -DBUILD_EXAMPLES=ON -DBUILD_MODULE_EXAMPLES=OFF
cmake --build /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320 --target \
  T6-http_client_awaitable T7-http_client_awaitable_edge_cases T19-ws_client T20-websocket_client \
  T25-h2c_client T45-h2_awaitable_surface T57-awaitable_v320_surface --parallel
/Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320/test/T57-awaitable_v320_surface
```

Expected: 新测试先失败。

**Step 3: 用 `galay-ssl` 模式改造协议 Awaitable**

重点：

- 消除直接窥探 `m_tasks` / `m_cursor`
- `SslRecvCompatAwaitable` 只保留兼容桥接，不再暴露底层队列实现
- 顺序读写链路改成新的状态机 / builder 风格

**Step 4: 重新跑第一轮核心回归**

Run:

```bash
for name in T6-http_client_awaitable T7-http_client_awaitable_edge_cases T19-ws_client T20-websocket_client T25-h2c_client T45-h2_awaitable_surface T57-awaitable_v320_surface; do
  /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320/test/$name
done
```

Expected: 全部通过。

**Step 5: 跑 HTTPS / H2 回归**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-http -B /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320-ssl \
  -DCMAKE_BUILD_TYPE=Release -DGALAY_HTTP_ENABLE_SSL=ON -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON -DBUILD_EXAMPLES=ON -DBUILD_MODULE_EXAMPLES=OFF
cmake --build /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320-ssl --target \
  T21-https_server T22-https_client T27-h2_server T28-h2_client --parallel
for name in T21-https_server T22-https_client T27-h2_server T28-h2_client; do
  /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320-ssl/test/$name
done
```

Expected: HTTPS / H2 关键回归通过。

**Step 6: 跑压测并保存原始输出**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-http
/Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320/benchmark/B1-HttpServer 8080 4 >/tmp/galay-http-b1.log 2>&1 &
HTTP_PID=$!
/Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320/benchmark/B2-HttpClient 127.0.0.1 8080 100 12 / \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-http/B2-HttpClient.txt
kill ${HTTP_PID}

/Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt/build-v320/benchmark/B15-HeaderParsing \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-http/B15-HeaderParsing.txt
```

Expected: benchmark 成功，输出已保存。

**Step 7: 更新版本、文档与 release notes**

- `CMakeLists.txt`：版本改为 `2.0.0`
- `galay-http/protoc/http/HttpBase.h`：同步 `GALAY_VERSION`
- `README.md`、`docs/02-API参考.md`、`docs/03-使用指南.md`、`docs/05-性能测试.md`
- `docs/releases/v2.0.0.md`

**Step 8: 中文提交**

Run:

```bash
git -C /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt add .
git -C /Users/gongzhijie/Desktop/projects/git/galay-http/.worktrees/kernel-v320-adapt commit -m "refactor: 适配 kernel v3.2.0 Awaitable 并补齐回归压测"
```

### Task 4: 第一波迁移 `galay-rpc`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/galay-rpc/kernel/RpcAwaitableBase.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/galay-rpc/kernel/RpcConn.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/galay-rpc/kernel/RpcClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/test/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/test/T4-rpc_awaitable_v320_surface.cpp`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/docs/03-使用指南.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/docs/releases/v2.0.0.md`

**Step 1: 写 failing test**

新增 `T4-rpc_awaitable_v320_surface.cpp`，覆盖：

- 请求/响应路径
- 复用同一连接再次请求
- 至少一个 stream 场景的顺序读写桥

**Step 2: 运行测试确认先失败**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-rpc -B /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320 --target \
  T2-rpc_server_test T3-rpc_client_test T4-rpc_awaitable_v320_surface \
  B1-RpcBenchServer B2-RpcBenchClient B4-RpcStreamBenchServer B5-RpcStreamBenchClient --parallel
ctest --test-dir /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320 \
  -R 'T2-rpc_server_test|T3-rpc_client_test|T4-rpc_awaitable_v320_surface' --output-on-failure
```

Expected: 新测试先失败。

**Step 3: 改造 RPC Awaitable 主路径**

重点：

- `WRITEV -> READV` 串联路径
- client request / response
- stream benchmark 路径

**Step 4: 跑核心回归**

Run:

```bash
ctest --test-dir /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320 \
  -R 'T2-rpc_server_test|T3-rpc_client_test|T4-rpc_awaitable_v320_surface' --output-on-failure
```

Expected: 全部通过。

**Step 5: 跑压测**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-rpc
/Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320/benchmark/B1-RpcBenchServer 9000 0 131072 >/tmp/galay-rpc-b1.log 2>&1 &
RPC_PID=$!
/Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320/benchmark/B2-RpcBenchClient -h 127.0.0.1 -p 9000 -c 200 -d 5 -s 47 -i 0 -l 4 -m unary \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-rpc/B2-RpcBenchClient.txt
kill ${RPC_PID}

/Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320/benchmark/B4-RpcStreamBenchServer 9100 0 131072 >/tmp/galay-rpc-b4.log 2>&1 &
STREAM_PID=$!
/Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt/build-v320/benchmark/B5-RpcStreamBenchClient -h 127.0.0.1 -p 9100 -c 100 -d 5 -s 128 -f 16 -w 8 -i 0 \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-rpc/B5-RpcStreamBenchClient.txt
kill ${STREAM_PID}
```

Expected: benchmark 成功，输出已保存。

**Step 6: 更新版本、文档与 release notes**

- `CMakeLists.txt` 改为 `2.0.0`
- 更新 README 与 docs
- 新增 `docs/releases/v2.0.0.md`

**Step 7: 中文提交**

Run:

```bash
git -C /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt add .
git -C /Users/gongzhijie/Desktop/projects/git/galay-rpc/.worktrees/kernel-v320-adapt commit -m "refactor: 适配 kernel v3.2.0 Awaitable 并完成验证"
```

### Task 5: 第二波并行迁移 `galay-mysql`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/galay-mysql/async/AsyncMysqlClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/galay-mysql/async/AsyncMysqlClient.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/test/T3-async_mysql_client.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/test/T5-connection_pool.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/test/T7-prepared_statement.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/docs/03-使用指南.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/docs/releases/v2.0.0.md`

**Step 1: 让异步路径先红**

在 `T3-async_mysql_client.cc` 或新子用例中覆盖：

- 连续两次 query
- prepare + execute
- 至少一个 pipeline / batch 链路

**Step 2: 跑测试确认失败**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-mysql -B ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DGALAY_MYSQL_BUILD_TESTS=ON -DGALAY_MYSQL_BUILD_EXAMPLES=ON -DGALAY_MYSQL_BUILD_BENCHMARKS=ON
cmake --build ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt/build-v320 --target T3-async_mysql_client T5-connection_pool T7-prepared_statement B2-AsyncPressure --parallel
GALAY_MYSQL_HOST=127.0.0.1 GALAY_MYSQL_PORT=3306 GALAY_MYSQL_USER=root GALAY_MYSQL_PASSWORD=password GALAY_MYSQL_DB=test \
ctest --test-dir ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt/build-v320 -R 'T3-async_mysql_client|T5-connection_pool|T7-prepared_statement' --output-on-failure
```

Expected: 新增异步覆盖先失败。

**Step 3: 改造 connect/auth/query/pipeline 路径**

要求：

- 不再继续堆叠旧 `addTask(...) + m_cursor`
- 顺序协议路径尽量对齐 `galay-ssl` 风格

**Step 4: 重新跑回归**

Run:

```bash
GALAY_MYSQL_HOST=127.0.0.1 GALAY_MYSQL_PORT=3306 GALAY_MYSQL_USER=root GALAY_MYSQL_PASSWORD=password GALAY_MYSQL_DB=test \
ctest --test-dir ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt/build-v320 -R 'T3-async_mysql_client|T5-connection_pool|T7-prepared_statement' --output-on-failure
```

Expected: 通过。

**Step 5: 跑压测**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mysql
GALAY_MYSQL_HOST=127.0.0.1 GALAY_MYSQL_PORT=3306 GALAY_MYSQL_USER=root GALAY_MYSQL_PASSWORD=password GALAY_MYSQL_DB=test \
~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt/build-v320/benchmark/B2-AsyncPressure \
  --clients 64 --queries 200 --warmup 50 --timeout-sec 180 --mode pipeline --batch-size 16 --buffer-size 16384 --sql "SELECT 1" \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mysql/B2-AsyncPressure.txt
```

Expected: 压测成功，输出已保存。

**Step 6: 更新版本与发布文档**

- `CMakeLists.txt` 改成 `project(galay-mysql VERSION 2.0.0 ...)`
- 更新 README / docs
- 写 `docs/releases/v2.0.0.md`

**Step 7: 中文提交**

Run:

```bash
git -C ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt add .
git -C ~/.config/superpowers/worktrees/galay-mysql/kernel-v320-adapt commit -m "refactor: 适配 kernel v3.2.0 Awaitable 并完成 MySQL 压测"
```

### Task 6: 第二波并行迁移 `galay-mongo`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/galay-mongo/async/AsyncMongoClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/galay-mongo/async/AsyncMongoClient.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/test/T3-async_mongo_pipeline.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/test/T5-async_mongo_functional.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/test/T6-auth_compatibility.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/docs/03-使用指南.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/docs/releases/v2.0.0.md`

**Step 1: 写 / 扩 failing 异步测试**

覆盖：

- 连续两次 command / ping
- 一次 pipeline
- 认证兼容

**Step 2: 先跑红灯**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-mongo -B ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DGALAY_MONGO_BUILD_TESTS=ON -DGALAY_MONGO_BUILD_EXAMPLES=ON -DGALAY_MONGO_BUILD_BENCHMARKS=ON
cmake --build ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt/build-v320 --target T3-async_mongo_pipeline T5-async_mongo_functional T6-auth_compatibility B2-AsyncPingBench --parallel
ctest --test-dir ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt/build-v320 -R 'T3-async_mongo_pipeline|T5-async_mongo_functional|T6-auth_compatibility' --output-on-failure
```

Expected: 新增异步覆盖先失败。

**Step 3: 改造 AsyncMongo 状态机**

重点：

- connect
- command
- pipeline
- recv-loop

**Step 4: 重新跑回归**

Run:

```bash
ctest --test-dir ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt/build-v320 -R 'T3-async_mongo_pipeline|T5-async_mongo_functional|T6-auth_compatibility' --output-on-failure
```

Expected: 通过。

**Step 5: 跑压测**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mongo
~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt/build-v320/benchmark/B2-AsyncPingBench \
  --total 8000 --concurrency 100 --host 140.143.142.251 --port 27017 --db admin --mode normal \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mongo/B2-AsyncPingBench.txt
```

Expected: 压测成功；在结果说明里注明远端网络环境。

**Step 6: 更新版本与发布文档**

- `CMakeLists.txt` 改为 `VERSION 2.0.0`
- 更新 README / docs
- 新建 `docs/releases/v2.0.0.md`

**Step 7: 中文提交**

Run:

```bash
git -C ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt add .
git -C ~/.config/superpowers/worktrees/galay-mongo/kernel-v320-adapt commit -m "refactor: 适配 kernel v3.2.0 Awaitable 并完成 Mongo 压测"
```

### Task 7: 第二波并行迁移 `galay-redis`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/galay-redis/async/RedisClient.h`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/galay-redis/async/RedisClient.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/test/T1-async.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/test/T8-redis_batch_timeout_api.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/test/T9-redis_batch_span_api.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/docs/02-API参考.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/docs/03-使用指南.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-redis/docs/releases/v2.0.0.md`

**Step 1: 写 / 扩 failing 测试**

覆盖：

- 连续 `command`
- `batch`
- `pipeline`

**Step 2: 先跑红灯**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-redis -B ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320 --target \
  T1-async T4-connection_pool T5-redis_client_timeout T8-redis_batch_timeout_api T9-redis_batch_span_api \
  B1-redis_client_bench B2-connection_pool_bench --parallel
ctest --test-dir ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320 \
  -R 'T1-async|T4-connection_pool|T5-redis_client_timeout|T8-redis_batch_timeout_api|T9-redis_batch_span_api' --output-on-failure
```

Expected: 新增异步覆盖先失败。

**Step 3: 改造 Redis Awaitable**

重点：

- command / batch / pipeline
- 顺序读写路径

**Step 4: 重新跑回归**

Run:

```bash
ctest --test-dir ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320 \
  -R 'T1-async|T4-connection_pool|T5-redis_client_timeout|T8-redis_batch_timeout_api|T9-redis_batch_span_api' --output-on-failure
```

Expected: 通过。

**Step 5: 跑压测**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-redis
~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320/benchmark/B1-redis_client_bench \
  -h 127.0.0.1 -p 6379 -c 10 -n 50000 -m pipeline -b 100 -q \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-redis/B1-redis_client_bench.txt
~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt/build-v320/benchmark/B2-connection_pool_bench \
  -h 127.0.0.1 -p 6379 -c 20 -n 300 -m 4 -x 20 -q \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-redis/B2-connection_pool_bench.txt
```

Expected: 两个 benchmark 成功。

**Step 6: 更新版本与发布文档**

- `CMakeLists.txt` 补为 `project(galay-redis VERSION 2.0.0 ...)`
- 更新 README / docs
- 新建 `docs/releases/v2.0.0.md`

**Step 7: 中文提交**

Run:

```bash
git -C ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt add .
git -C ~/.config/superpowers/worktrees/galay-redis/kernel-v320-adapt commit -m "refactor: 适配 kernel v3.2.0 Awaitable 并完成 Redis 压测"
```

### Task 8: 第三波兼容验证 `galay-etcd`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/galay-etcd/async/AsyncEtcdClient.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/docs/releases/v2.0.0.md`

**Step 1: 先跑现有 async 测试**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-etcd -B ~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320 \
  -DCMAKE_BUILD_TYPE=Release -DGALAY_ETCD_BUILD_TESTS=ON -DGALAY_ETCD_BUILD_BENCHMARKS=ON -DGALAY_ETCD_BUILD_EXAMPLES=ON
cmake --build ~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320 --target \
  T4-AsyncEtcdSmoke T5-AsyncEtcdPipeline T6-EtcdInternalHelpers B1-EtcdKvBenchmark --parallel
~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320/test/T6-EtcdInternalHelpers
~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320/test/T4-AsyncEtcdSmoke http://127.0.0.1:2379
~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320/test/T5-AsyncEtcdPipeline http://127.0.0.1:2379
```

Expected: 若已兼容则直接通过。

**Step 2: 仅在失败时做最小修补**

如果失败，再最小修改 `AsyncEtcdClient.cc` 相关 Awaitable 适配点。

**Step 3: 跑 benchmark**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-etcd
~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt/build-v320/benchmark/B1-EtcdKvBenchmark \
  http://127.0.0.1:2379 8 500 64 put \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-etcd/B1-EtcdKvBenchmark-put.txt
```

Expected: benchmark 成功；若代码零改动，则只归档结果、不发版。

**Step 4: 只有出现实际改动才升级版本并提交**

Run:

```bash
git -C ~/.config/superpowers/worktrees/galay-etcd/kernel-v320-adapt status --short
```

Expected:

- 如果无 diff：停止在验证层，不 bump 版本、不打 tag、不发 release
- 如果有 diff：改到 `2.0.0`、更新文档、中文提交

### Task 9: 第三波兼容验证 `galay-mcp`

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/galay-mcp/client/McpHttpClient.cc`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/README.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/docs/05-性能测试.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/CMakeLists.txt`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/docs/releases/v2.0.0.md`

**Step 1: 先跑现有 stdio / HTTP 回归**

Run:

```bash
cmake -S /Users/gongzhijie/Desktop/projects/git/galay-mcp -B ~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320 -DBUILD_MODULE_EXAMPLES=OFF
cmake --build ~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320 --target \
  T1-stdio_client T2-stdio_server T3-http_client T4-http_server \
  B1-stdio_performance B2-http_performance B3-concurrent_requests --parallel

mkfifo /tmp/galay-mcp-c2s /tmp/galay-mcp-s2c
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T2-stdio_server < /tmp/galay-mcp-c2s > /tmp/galay-mcp-s2c &
STDIO_PID=$!
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T1-stdio_client > /tmp/galay-mcp-c2s < /tmp/galay-mcp-s2c
kill ${STDIO_PID}
rm -f /tmp/galay-mcp-c2s /tmp/galay-mcp-s2c

~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T4-http_server 8080 0.0.0.0 >/tmp/galay-mcp-http.log 2>&1 &
HTTP_PID=$!
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T3-http_client http://127.0.0.1:8080/mcp
kill ${HTTP_PID}
```

Expected: 回归通过。

**Step 2: 仅在失败时修补 HTTP Awaitable 调用点**

优先检查 `McpHttpClient.cc` 对新 kernel Awaitable 口径的调用。

**Step 3: 跑 benchmark**

Run:

```bash
mkdir -p /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mcp
mkfifo /tmp/galay-b1-c2s /tmp/galay-b1-s2c
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T2-stdio_server < /tmp/galay-b1-c2s > /tmp/galay-b1-s2c &
STDIO_PID=$!
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/B1-stdio_performance 1000 > /tmp/galay-b1-c2s < /tmp/galay-b1-s2c \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mcp/B1-stdio_performance.txt
kill ${STDIO_PID}
rm -f /tmp/galay-b1-c2s /tmp/galay-b1-s2c

~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/T4-http_server 8080 0.0.0.0 >/tmp/galay-mcp-bench-http.log 2>&1 &
HTTP_PID=$!
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/B2-http_performance --url http://127.0.0.1:8080/mcp --connections 32 --requests 1000 --io 4 --compute 2 \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mcp/B2-http_performance.txt
~/.config/superpowers/worktrees/galay-mcp/kernel-v320-adapt/build-v320/bin/B3-concurrent_requests --url http://127.0.0.1:8080/mcp --workers 10 --requests 100 \
  | tee /Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/galay-mcp/B3-concurrent_requests.txt
kill ${HTTP_PID}
```

Expected: benchmark 成功；若代码零改动，则只归档结果、不发版。

**Step 4: 只有出现实际改动才升级版本并提交**

与 `galay-etcd` 相同，零 diff 则不发版。

### Task 10: 为所有实际改动仓库打大版本 tag 并发 release

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/docs/releases/v2.0.0.md`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/docs/releases/v2.0.0.md`

**Step 1: 列出实际改动仓库**

Run:

```bash
for d in galay-ssl galay-http galay-rpc galay-mysql galay-mongo galay-redis galay-etcd galay-mcp; do
  printf '=== %s ===\n' "$d"
  git -C /Users/gongzhijie/Desktop/projects/git/$d status --short
done
```

Expected: 只把有实际提交的仓库纳入发布清单。

**Step 2: 最终核对 release notes**

每个 `docs/releases/v2.0.0.md` 必须包含：

- 适配点
- 回归命令与结果
- 压测命令与结果摘要
- 升级注意事项

**Step 3: 创建 annotated tag**

模板：

```bash
git -C <worktree> tag -a v2.0.0 -m "v2.0.0

适配 galay-kernel v3.2.0 Awaitable
- 收敛旧状态机写法
- 完成 fresh 回归
- 完成 fresh 压测
- 同步文档与升级说明"
```

**Step 4: 发布 GitHub Release**

模板：

```bash
gh release create v2.0.0 \
  --repo gzj-creator/<repo> \
  --title "v2.0.0" \
  --notes-file <worktree>/docs/releases/v2.0.0.md
```

Expected: 对每个实际改动仓库 release 创建成功。

### Task 11: 产出总汇总

**Files:**
- Create: `/Users/gongzhijie/Desktop/projects/git/test_results/kernel-v320-rollout-2026-03-18/final-summary.md`
- Create: `/Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/kernel-v320-rollout/docs/plans/2026-03-18-kernel-v320-galay-rollout-results.md`

**Step 1: 写最终汇总**

在两个结果文件中统一记录：

- 哪些仓库实际改动
- 对应提交 SHA
- 新 tag
- release 链接
- fresh 回归状态
- fresh 压测状态
- 是否能给出量化性能提升

**Step 2: 在总控 worktree 提交结果文档**

Run:

```bash
git -C /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/kernel-v320-rollout add \
  docs/plans/2026-03-18-kernel-v320-galay-rollout-results.md
git -C /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/kernel-v320-rollout commit -m "docs: 汇总 kernel v3.2.0 多仓适配结果"
```

Expected: 总结果文档形成独立提交。
