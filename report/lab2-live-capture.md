# 实验二实际运行记录

## 运行环境与命令

- 记录日期：2026-11-17
- Npcap：1.88；基于 libpcap 1.10.6，64-bit `time_t`
- 捕获设备：本次枚举列表中的第 4 项，`Realtek 8822CE Wireless LAN 802.11ac PCI-E NIC`
- 捕获参数：`--device-index 4 --count 20`
- 混杂模式：关闭

在 WSL Bash 中构建程序并查看 PE 格式：

```bash
cd ~/projects/network-lab/workspace
make
file build/lab2/npcap_capture.exe
```

本次 Windows 调用的核心命令参数如下。设备编号只对本次枚举有效；再次运行前应先查看设备列表：

```text
--device-index 4 --count 20
```

## 实际结果

程序成功列出 11 个捕获设备，打开本次列表中的 Realtek 无线网卡，并报告：

```text
Link type: DLT_EN10MB (Ethernet); promiscuous mode: off
Stop with Ctrl+C; displaying 20 valid frames.
```

共显示 20 帧，跳过 0 个短帧，程序正常结束，`ExitCode=0`。捕获记录的时间为 15:31:43.036 至 15:31:43.042。每条记录都显示了源 MAC、目的 MAC、Type/Length、Type/Length 后第一个字节和捕获长度。

从实际输出中摘录两条协议字段用于说明解析结果（MAC 地址不在此重复记录）：

| 帧 | Type/Length | 紧随其后的字节 | 捕获长度 | 对应内容 |
| ---: | ---: | ---: | ---: | --- |
| 1 | `0x0800` | `0x45` | 56 字节 | IPv4 帧，偏移 14 首字节为 `0x45` |
| 3 | `0x86DD` | `0x60` | 170 字节 | IPv6 帧，偏移 14 首字节为 `0x60` |

这两条记录验证了 Type/Length 的网络字节序显示，以及程序始终从偏移 14 读取后续第一个字节。完整终端输出由用户在本次会话中提供。

## 验收状态

- Windows Npcap 设备枚举：通过，列出 11 个设备。
- 真实无线网卡打开与链路类型检查：通过，链路类型为 `DLT_EN10MB`。
- 有限实时捕获：通过，显示 20 帧并以退出码 0 结束。
- 四项字段显示：通过；IPv4、IPv6 示例字段如上。
- 短帧处理：本次捕获中未遇到短帧；独立解析器边界用例此前验证过 0、14、15 字节长度。
- Wireshark 同帧对照和正式实验报告：尚未完成，不能据此记录为通过。

后续进行 Wireshark 对照时，应选择可可靠关联的同一帧，并分别核对源/目的 MAC、Type/Length 和偏移 14 的字节；保存对应截图供正式实验报告使用。
