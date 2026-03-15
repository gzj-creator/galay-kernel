# Scheduler V3 Latency Tuning Design

## Problem

当前 `v3` 的 fixed ready budget 把纯 remote injected burst 也切成多轮 `processPendingCoroutines()` pass。
这改善了公平性，但把 baseline 原本接近一次 drain 完成的 burst 场景，拉成了“执行一批 -> tick/poll 边界 -> 再执行下一批”的节奏，导致 `B14` injected latency 明显回退。

## Options

### 方案 A：直接缩小/放大全局 `batch_size`
- 优点：实现最简单
- 缺点：只能在吞吐/延迟之间全局摇摆，不能区分 local-ready 和 pure-injected 两类场景

### 方案 B：为纯 injected burst 增加自适应 fast path（推荐）
- 当一个 pass 开始时没有本地 ready work，说明当前主要是在处理 remote injected backlog
- 这类 backlog 不应被固定 `ready_budget` 人为切成多轮
- 做法：保留 `ready_budget` 作为默认公平性边界，但给“本轮 drain 进来的 injected work”发放 burst credit；credit 未耗尽前，允许当前 pass 继续运行
- 这样 local self-reschedule 仍受 budget 约束，而 pure injected burst 可以更接近 baseline 的单 pass drain 行为

### 方案 C：改成时间片预算
- 优点：更贴近 tail-latency 控制
- 缺点：实现和验证都更复杂，当前迭代不划算

## Chosen Design

采用方案 B：

- 保留 `ready_budget`
- 当 `processPendingCoroutines()` 开始时若 `m_worker.hasLocalWork() == false`，启用 injected burst fast path
- 每次 `drainInjected()` 返回 `count > 0` 时，累计 `burst_credit += count`
- 每 resume 一个 task 时消耗一个 credit
- 当 `ran >= ready_budget` 且 `burst_credit == 0` 时退出本轮 pass
- 这样可以让“纯 injected backlog”在单轮中继续推进，而不会让自生成 local-ready work 无限逃逸公平性边界

## Tests

- 现有 `T45` 改为验证“local ready backlog”仍然受 budget 限制
- 新增 injected backlog 场景：在没有本地 ready work 的前提下，单轮 `processPendingCoroutines()` 应能完成全部 injected tasks

## Expected Outcome

- `B14` injected latency 明显回收，向 baseline 靠近
- `B14` throughput 维持高位，不退回 baseline 的 notify 风暴
- `T45` / 新增 injected fast-path 测试共同约束公平性与延迟折中
