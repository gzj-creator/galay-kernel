# Awaitable 示例选型指南设计

## 背景

`galay-kernel` 现在已经有两类正式示例：

- `E10-custom_awaitable`：状态机式自定义 Awaitable
- `E11-builder_protocol`：链式 Builder 协议解析

但 `docs/03-使用指南.md` 目前仍然是 API 片段级说明，用户能看到两种写法，却不容易快速判断“什么时候选哪一种”。

## 目标

在 `docs/03-使用指南.md` 里补一个紧凑的“选型指南”区块，做到：

1. 让用户先看例子，再决定走哪条路
2. 说明状态机和 Builder 的分工边界
3. 直接把 `E10`、`E11` 变成文档里的第一跳入口

## 非目标

- 不新增 API
- 不重复粘贴整份示例源码
- 不把这部分扩写成独立专题页

## 方案

### 方案 A：只补示例链接

优点：

- 变更最小

缺点：

- 不能回答“为什么选它”
- 用户仍要自己从例子里推导分工

### 方案 B：补一个简短选型小节，并列 `E10` / `E11`

优点：

- 信息密度高
- 与已有 API 片段互补
- 后续容易同步到 README / API 参考

缺点：

- 文档略长一点

### 结论

采用方案 B。

## 设计

在 `docs/03-使用指南.md` 的“构建协议解析型组合 Awaitable”小节内，放在“当前实现说明”和“进一步阅读”之间，新增一个“如何选择：状态机还是 Builder”区块。

内容包括：

- `E10` / `E11` 的一句话定位
- 一组简明的选型建议：
  - 线性协议、半包/粘包解析优先 Builder
  - connect/read/write/shutdown/handshake 多状态切换优先状态机
  - 需要显式 `ops.queue(...)` 或持有步骤对象时改用 `SequenceAwaitable + SequenceStep`
- 一个最小对照代码骨架：
  - 状态机入口：`AwaitableBuilder<Result>::fromStateMachine(...)`
  - Builder 入口：`.recv(...).parse(...).send(...).build()`

## 验证

至少验证：

```bash
rg -n "E10-custom_awaitable|E11-builder_protocol|如何选择：状态机还是 Builder" docs/03-使用指南.md
git diff --check -- docs/03-使用指南.md
```

通过标准：

- 文档新增区块存在
- `E10` / `E11` 两个示例都被正确引用
- 无格式错误
