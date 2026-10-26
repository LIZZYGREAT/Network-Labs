# 抓包权限记录

- 尝试日期：2026-10-20
- 接口：WSL2 回环接口 `lo`
- 输出文件目标：`experiment_records/chatroom-loopback.pcap`

直接运行 tcpdump 时，工具返回：

```text
tcpdump: lo: You don't have permission to capture on that device
(socket: Operation not permitted)
```

经授权环境执行 `sudo -n tcpdump` 时返回：

```text
sudo: a password is required
```

因此没有生成 pcap。本项目没有把 TCP_INFO 统计冒充为逐包抓包结果。具备抓包权限时，可按 README 的命令重新采集并用 Wireshark 查看 `tcp.port == 8888`。
