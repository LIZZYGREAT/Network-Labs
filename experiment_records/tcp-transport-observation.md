# TCP 传输层观测记录

- 测试时间：2026-10-28 17:02:17 CST +0800
- 测试地址：127.0.0.1:8888
- 数据来源：Linux `TCP_INFO` 每条测试 TCP Socket 的 `tcpi_total_retrans` 累计计数。
- 解释：这是连接累计计数，不是对每个 TCP 段的完整抓包分类。

| 客户端连接 | TCP 重传累计数 |
| --- | ---: |
| Alice | 0 |
| Bob-relogin | 0 |
| Charlie | 0 |

此探测脚本读取 TCP_INFO，不启动抓包工具；pcap 需按 README 中的独立命令采集。
