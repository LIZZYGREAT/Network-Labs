# 实验环境记录

- 记录日期：2026-10-20（Asia/Shanghai，UTC+08:00）
- 操作系统：Ubuntu 22.04.5 LTS，WSL2
- 内核：Linux 6.18.33.2-microsoft-standard-WSL2，x86_64
- 编译器：g++ 11.4.0（Ubuntu 11.4.0-1ubuntu1~22.04.2）
- 构建工具：GNU Make 4.3
- 测试辅助环境：Python 3.10.12
- 聊天网络：同一 WSL 环境中的 IPv4 回环地址 `127.0.0.1`
- 时间：服务器使用本地时间；本机记录为 CST `+0800`
- 抓包工具：系统可找到 `tcpdump`，但当前环境缺少对 `lo` 抓包所需权限

构建命令：

```bash
make clean && make
```

实测结果：服务端和客户端均成功编译；`-Wall -Wextra -Wpedantic` 未输出警告。功能探测命令为 `python3 tests/protocol_probe.py`，测试自动挑选临时本机端口；最近一次记录见 `functional-test-results.md`。

抓包尝试：普通 `tcpdump -i lo` 返回 `Operation not permitted`；`sudo -n tcpdump` 返回 `a password is required`。未生成 pcap 文件。
