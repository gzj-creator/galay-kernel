# 09-UDP性能测试

本页现在只保留 UDP 数据面的补充定位；当前 fresh 性能事实请优先回到主干页。

## 本页回答什么

- 仓库里有哪些 UDP 相关 benchmark / test / example
- 哪些结果是当前 fresh evidence，哪些只是入口保留
- 先去哪里看 `UdpSocket` 的真实用法

## 当前稳定事实

- `UdpSocket` 是公开 API，接口与边界以 `docs/02-API参考.md` 为准
- 2026-03-10 这一轮没有重新采样 UDP benchmark 数字
- UDP 相关 benchmark / test / example 仍是仓库中的真实资产，但当前主干只把它们当作入口与验证锚点，不把旧数字当作现时承诺

## 先看主干页

- API 与边界：`docs/02-API参考.md`
- UDP 工作流：`docs/03-使用指南.md`
- 当前性能事实：`docs/05-性能测试.md`
- 平台与 IO 背景：`docs/06-高级主题.md`

## 源码 / 验证锚点

- 源码：`galay-kernel/async/UdpSocket.h`、`galay-kernel/async/UdpSocket.cc`
- 测试：`test/T5-udp_socket.cc`、`test/T6-udp_server.cc`、`test/T7-udp_client.cc`
- 示例：`examples/include/E5-udp_echo.cc`
- benchmark：`benchmark/B4-udp_server.cc`、`benchmark/B5-udp_client.cc`、`benchmark/B6-Udp.cc`

## RAG 关键词

- `UdpSocket`
- `UDP echo`
- `B6-Udp`
- `T5-udp_socket`
- `E5-UdpEcho`
