# README Awaitable 入口设计

## 背景

`docs/03-使用指南.md` 已经补了 `E10-custom_awaitable` 与 `E11-builder_protocol` 的并列选型说明，但 `README.md` 作为首页入口，当前还停留在 API/能力层的概述。

这会导致第一次进入仓库的用户虽然知道“现在有状态机和 Builder 两种模型”，却不知道应该优先点开哪个例子。

## 目标

在 `README.md` 增加一个非常短的 Awaitable 入门入口，做到：

1. 首页直接告诉用户先看哪个示例
2. 用最少文字区分状态机和 Builder 的适用场景
3. 不和 `docs/03-使用指南.md` 重复堆叠实现细节

## 非目标

- 不把 `docs/03` 的选型区块整段复制到 `README`
- 不新增 API 说明
- 不扩展更多示例链接，只聚焦 `E10` 与 `E11`

## 方案

### 方案 A：精简入口版

内容只包含：

- `E10` 的一句话定位
- `E11` 的一句话定位
- 2 到 3 条简短选型提示

优点：

- 适合首页
- 不与 `docs/03` 重复
- 后续维护成本低

缺点：

- 细节仍需跳转到 `docs/03` 或示例源码

### 方案 B：详细同步版

把 `docs/03` 的“如何选择：状态机还是 Builder”几乎原样搬到 `README`。

优点：

- 首页自包含

缺点：

- 首页显著变长
- 容易与 `docs/03` 漂移

### 结论

采用方案 A。

## 设计

在 `README.md` 的 `v3.1.0 非兼容升级` 小节末尾，新增一个极短的“Awaitable 入门”区块：

- 指向 `examples/include/E10-custom_awaitable.cc`
- 指向 `examples/include/E11-builder_protocol.cc`
- 说明：
  - 多状态推进优先状态机
  - 线性协议解析优先 Builder
  - 更细的扩展和约束仍看 `docs/03-使用指南.md`

## 验证

至少验证：

```bash
rg -n "E10-custom_awaitable|E11-builder_protocol|Awaitable 入门" README.md
git diff --check -- README.md
```

通过标准：

- README 中存在新入口
- 两个示例都被正确引用
- 无格式问题
