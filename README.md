# TCP Socket 多人聊天室

基于 Linux / WSL、C++17、POSIX TCP Socket 和标准线程库实现的命令行聊天室。服务器集中转发公开消息；客户端使用 UTF-8；每个连接使用长度前缀应用层帧。

## 环境与编译

需要 `g++`、GNU Make 和 Python 3（Python 只用于协议边界测试）：

```bash
make
```

编译服务器和客户端：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
    src/server.cpp src/protocol.cpp -o server
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
    src/client.cpp src/protocol.cpp -o client
```

`make clean` 删除本地编译生成的 `server` 和 `client`。

## 运行

终端 1 启动服务器：

```bash
./server 8888
```

终端 2～4 分别启动客户端：

```bash
./client 127.0.0.1 8888 Alice
./client 127.0.0.1 8888 Bob
./client 127.0.0.1 8888 Charlie
```

输入一行文字并回车即可广播；输入 `/quit` 或按 Ctrl+D 离开。服务器按 Ctrl+C 停止。服务端监听 IPv4 所有本机地址，客户端参数为服务器 IPv4 地址、端口和用户名。端口范围为 `1～65535`，用户名为 `1～32` 个 UTF-8 字节，在线用户不能重名。

消息由服务器返回后显示，格式为：

```text
[2026-10-20 17:12:00] Alice: 你好，Hello!
```

时间戳采用服务器本地时区。协议字段和异常行为见 [docs/protocol.md](docs/protocol.md)，架构和互斥策略见 [docs/design.md](docs/design.md)。

## 验收与抓包

运行协议与多人功能探测脚本（它会选择本机临时端口、启动服务端、完成测试并写入简明结果）：

```bash
python3 tests/protocol_probe.py
```

测试结果与服务端日志保存在 `experiment_records/`。本机回环抓包可用：

```bash
sudo tcpdump -i lo -s 0 -w experiment_records/chatroom-loopback.pcap 'tcp port 8888'
```

开始抓包后，在其他终端按上面的命令启动服务器和客户端、发送几条消息，之后在 tcpdump 终端按 Ctrl+C。Wireshark 过滤器为 `tcp.port == 8888`。回环网络未观察到重传也是有效结果；不能把一次 `send()` 当成一个 TCP 段。

## 项目文件

```text
src/                    C++ 服务端、客户端和协议实现
tests/protocol_probe.py 多人、帧边界与异常输入探测
docs/                   协议及架构说明
experiment_records/     可用于实验报告的实测记录和抓包
report/report.md        实验报告
```
