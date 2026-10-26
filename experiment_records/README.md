# 实验数据与记录

此目录只保留可用于实验报告的材料：

- `environment.md`：实际系统、工具版本、构建命令和测试环境。
- `functional-test-results.md`：测试编号、结果、时间及实际监听端口。
- `test-observations.md`：关键输入与实际观察，含广播条数和异常行为。
- `tcp-transport-observation.md`：每条测试连接的 TCP_INFO 重传计数。
- `capture-availability.md`：本次抓包尝试的实际权限结果。
- `server-test.log`：该轮自动化功能测试对应的服务端运行日志。

测试记录由 `python3 tests/protocol_probe.py` 生成并更新。没有保存空的 pcap 或虚构截图；本机当前账号缺少回环抓包权限，实际尝试结果单独记在 `capture-availability.md`。
