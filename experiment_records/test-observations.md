# 测试输入与观察结果

- 对应测试时间：2026-10-28 17:02:17 CST +0800
- 对应测试地址：127.0.0.1:8888

| 编号 | 输入 / 操作 | 实际观察 |
| --- | --- | --- |
| T00 | 启动服务端并连接 127.0.0.1:8888。 | 本机 TCP connect 成功，服务端保持运行并接受后续客户端。 |
| T01 | Alice 的 LOGIN 分 3 次写入；Bob、Charlie 正常登录。 | 三人均收到空 Data 的 LOGIN_OK。 |
| T02 | Alice 在线时再次 LOGIN(Alice)；另发空用户名 LOGIN。 | 分别收到 ERROR: username already in use 和 ERROR: invalid username。 |
| T03 | Alice 一次 sendall 写入“你好，Hello!”与“第二条消息 / second message”两帧。 | Alice、Bob、Charlie 各按顺序收到两条，核对 6/6 条正文、用户名和时间字段。 |
| T04 | Bob 发送“Bob says: TCP 聊天正常”。 | 3/3 客户端收到完整中英文正文；发送者为 Bob，时间字段符合格式。 |
| T05 | Bob 发送 QUIT；Charlie 随后发送“Alice and Charlie continue after Bob exits”；再以 Bob 登录。 | Bob 连接关闭；Alice 与 Charlie 各收到后续消息；Bob 用户名可再次登录。 |
| T06 | 登录用户发送单字节 0xff；未登录连接直接发送 CHAT。 | 分别收到 ERROR: invalid message 和 ERROR: LOGIN required；未产生广播。 |
| T07 | 发送 BodyLength=4097；另连接声明长度 5，只发送 Type 后半关闭。 | 两连接均被关闭；未收到不完整消息。 |
| T08 | 未登录连接发送未知类型 0x7f。 | 收到 ERROR: LOGIN required，连接按协议拒绝。 |
| T09 | 启动 C++ client，发送“client CLI 中文消息”后输入 /quit。 | 客户端输出自身服务器广播，收到的正文完整，进程以状态码 0 退出。 |
| N01 | 采样仍在线的 Alice、重新登录的 Bob 和 Charlie TCP Socket。 | Alice=0, Bob-relogin=0, Charlie=0（tcpi_total_retrans 累计值） |
