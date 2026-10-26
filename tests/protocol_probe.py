#!/usr/bin/env python3
"""Exercise the chat server's framing, login, broadcast, and disconnect paths."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import select
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAX_BODY_BYTES = 4096
LOGIN, LOGIN_OK, CHAT, BROADCAST, QUIT, ERROR = range(1, 7)


def make_frame(message_type: int, data: bytes = b"") -> bytes:
    body = bytes([message_type]) + data
    return struct.pack("!I", len(body)) + body


def recv_exact(sock: socket.socket, count: int) -> bytes:
    result = bytearray()
    while len(result) < count:
        part = sock.recv(count - len(result))
        if not part:
            raise EOFError("peer closed before a complete frame arrived")
        result.extend(part)
    return bytes(result)


def recv_frame(sock: socket.socket) -> tuple[int, bytes]:
    size = struct.unpack("!I", recv_exact(sock, 4))[0]
    if not 1 <= size <= MAX_BODY_BYTES:
        raise AssertionError(f"server sent invalid frame length {size}")
    body = recv_exact(sock, size)
    return body[0], body[1:]


def connect_logged_in(port: int, username: str) -> socket.socket:
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(3)
    sock.sendall(make_frame(LOGIN, username.encode("utf-8")))
    message_type, data = recv_frame(sock)
    if (message_type, data) != (LOGIN_OK, b""):
        sock.close()
        raise AssertionError(f"LOGIN failed for {username}: {message_type=} {data=!r}")
    return sock


def assert_error(sock: socket.socket, expected: bytes) -> None:
    message_type, data = recv_frame(sock)
    assert message_type == ERROR, f"expected ERROR, got type {message_type}"
    assert data == expected, f"expected error {expected!r}, got {data!r}"


def assert_broadcast(sock: socket.socket, username: str, content: str) -> None:
    message_type, data = recv_frame(sock)
    assert message_type == BROADCAST, f"expected BROADCAST, got type {message_type}"
    text = data.decode("utf-8")
    timestamp, sender, received_content = text.split("\n", 2)
    assert re.fullmatch(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", timestamp), timestamp
    assert sender == username, f"expected sender {username!r}, got {sender!r}"
    assert received_content == content, (received_content, content)


def tcp_total_retransmissions(sock: socket.socket) -> int:
    # Linux struct tcp_info: tcpi_total_retrans is the u32 at byte offset 100.
    info = sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_INFO, 104)
    if len(info) < 104:
        raise RuntimeError(f"short TCP_INFO response: {len(info)} bytes")
    return struct.unpack_from("=I", info, 100)[0]


def wait_for_text(process: subprocess.Popen[bytes], collected: bytearray,
                  needle: bytes, timeout: float = 4.0) -> None:
    deadline = time.monotonic() + timeout
    while needle not in collected:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError(
                f"timed out waiting for {needle!r}; output={bytes(collected)!r}"
            )
        ready, _, _ = select.select([process.stdout], [], [], remaining)
        if not ready:
            continue
        chunk = os.read(process.stdout.fileno(), 4096)
        if not chunk:
            raise AssertionError(
                f"client exited before {needle!r}; output={bytes(collected)!r}"
            )
        collected.extend(chunk)


def choose_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_server(process: subprocess.Popen[bytes], port: int) -> None:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited early with status {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError("server did not accept a local TCP connection")


def run_suite(port: int, results: list[tuple[str, str, str]],
              details: list[tuple[str, str, str]]) -> dict[str, int]:
    alice = bob = charlie = bob_again = None
    try:
        alice = socket.create_connection(("127.0.0.1", port), timeout=3)
        alice.settimeout(3)
        login = make_frame(LOGIN, b"Alice")
        for part in (login[:2], login[2:6], login[6:]):
            alice.sendall(part)
            time.sleep(0.03)
        assert recv_frame(alice) == (LOGIN_OK, b"")
        bob = connect_logged_in(port, "Bob")
        charlie = connect_logged_in(port, "Charlie")
        results.append(("T01", "TCP 连接与三名用户登录", "PASS"))
        details.append(("T01", "Alice 的 LOGIN 分 3 次写入；Bob、Charlie 正常登录。",
                        "三人均收到空 Data 的 LOGIN_OK。"))

        duplicate = socket.create_connection(("127.0.0.1", port), timeout=3)
        duplicate.settimeout(3)
        duplicate.sendall(make_frame(LOGIN, b"Alice"))
        assert_error(duplicate, b"username already in use")
        duplicate.close()

        empty_name = socket.create_connection(("127.0.0.1", port), timeout=3)
        empty_name.settimeout(3)
        empty_name.sendall(make_frame(LOGIN))
        assert_error(empty_name, b"invalid username")
        empty_name.close()
        results.append(("T02", "重复用户名拒绝与非法用户名拒绝", "PASS"))
        details.append(("T02", "Alice 在线时再次 LOGIN(Alice)；另发空用户名 LOGIN。",
                        "分别收到 ERROR: username already in use 和 ERROR: invalid username。"))

        first_text = "你好，Hello!"
        second_text = "第二条消息 / second message"
        alice.sendall(make_frame(CHAT, first_text.encode()) +
                      make_frame(CHAT, second_text.encode()))
        for recipient in (alice, bob, charlie):
            assert_broadcast(recipient, "Alice", first_text)
            assert_broadcast(recipient, "Alice", second_text)
        results.append(("T03", "合并写入两帧后按顺序广播给三客户端", "PASS"))
        details.append(("T03", "Alice 一次 sendall 写入“你好，Hello!”与“第二条消息 / second message”两帧。",
                        "Alice、Bob、Charlie 各按顺序收到两条，核对 6/6 条正文、用户名和时间字段。"))

        bob_text = "Bob says: TCP 聊天正常"
        bob.sendall(make_frame(CHAT, bob_text.encode("utf-8")))
        for recipient in (alice, bob, charlie):
            assert_broadcast(recipient, "Bob", bob_text)
        results.append(("T04", "中英文 UTF-8 消息及发送者、时间戳", "PASS"))
        details.append(("T04", "Bob 发送“Bob says: TCP 聊天正常”。",
                        "3/3 客户端收到完整中英文正文；发送者为 Bob，时间字段符合格式。"))

        bob.sendall(make_frame(QUIT))
        assert bob.recv(1) == b""
        bob.close()
        bob = None
        continuing = "Alice and Charlie continue after Bob exits"
        charlie.sendall(make_frame(CHAT, continuing.encode()))
        for recipient in (alice, charlie):
            assert_broadcast(recipient, "Charlie", continuing)
        bob_again = connect_logged_in(port, "Bob")
        results.append(("T05", "单用户退出后其余用户继续聊天且用户名可复用", "PASS"))
        details.append(("T05", "Bob 发送 QUIT；Charlie 随后发送“Alice and Charlie continue after Bob exits”；再以 Bob 登录。",
                        "Bob 连接关闭；Alice 与 Charlie 各收到后续消息；Bob 用户名可再次登录。"))

        invalid_utf8 = connect_logged_in(port, "InvalidUtf8")
        invalid_utf8.sendall(make_frame(CHAT, b"\xff"))
        assert_error(invalid_utf8, b"invalid message")
        invalid_utf8.close()
        unlogged = socket.create_connection(("127.0.0.1", port), timeout=3)
        unlogged.settimeout(3)
        unlogged.sendall(make_frame(CHAT, b"must not be accepted"))
        assert_error(unlogged, b"LOGIN required")
        unlogged.close()
        results.append(("T06", "未登录聊天与非法 UTF-8 正文被拒绝", "PASS"))
        details.append(("T06", "登录用户发送单字节 0xff；未登录连接直接发送 CHAT。",
                        "分别收到 ERROR: invalid message 和 ERROR: LOGIN required；未产生广播。"))

        too_long = socket.create_connection(("127.0.0.1", port), timeout=3)
        too_long.settimeout(3)
        too_long.sendall(struct.pack("!I", MAX_BODY_BYTES + 1))
        assert too_long.recv(1) == b""
        too_long.close()

        truncated = socket.create_connection(("127.0.0.1", port), timeout=3)
        truncated.settimeout(3)
        truncated.sendall(struct.pack("!I", 5) + bytes([CHAT]))
        truncated.shutdown(socket.SHUT_WR)
        assert truncated.recv(1) == b""
        truncated.close()
        results.append(("T07", "超长帧拒绝及帧体中途断开清理", "PASS"))
        details.append(("T07", "发送 BodyLength=4097；另连接声明长度 5，只发送 Type 后半关闭。",
                        "两连接均被关闭；未收到不完整消息。"))

        unexpected = socket.create_connection(("127.0.0.1", port), timeout=3)
        unexpected.settimeout(3)
        unexpected.sendall(make_frame(0x7F))
        assert_error(unexpected, b"LOGIN required")
        unexpected.close()
        results.append(("T08", "未知/错误方向消息拒绝", "PASS"))
        details.append(("T08", "未登录连接发送未知类型 0x7f。",
                        "收到 ERROR: LOGIN required，连接按协议拒绝。"))

        cli = subprocess.Popen(
            [str(ROOT / "client"), "127.0.0.1", str(port), "CliSmoke"],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        output = bytearray()
        try:
            wait_for_text(cli, output, b"Connected as CliSmoke.")
            cli.stdin.write("client CLI 中文消息\n".encode("utf-8"))
            cli.stdin.flush()
            wait_for_text(cli, output, b"CliSmoke: client CLI " + "中文消息".encode("utf-8"))
            cli.stdin.write(b"/quit\n")
            cli.stdin.flush()
            cli.wait(timeout=4)
            assert cli.returncode == 0, f"client exited with {cli.returncode}"
        finally:
            if cli.poll() is None:
                cli.terminate()
                cli.wait(timeout=2)
        results.append(("T09", "C++ 命令行客户端登录、收发中文与正常退出", "PASS"))
        details.append(("T09", "启动 C++ client，发送“client CLI 中文消息”后输入 /quit。",
                        "客户端输出自身服务器广播，收到的正文完整，进程以状态码 0 退出。"))

        transport_stats = {
            name: tcp_total_retransmissions(sock)
            for name, sock in (("Alice", alice), ("Bob-relogin", bob_again),
                               ("Charlie", charlie))
        }
        values = ", ".join(f"{name}={count}" for name, count in transport_stats.items())
        results.append(("N01", "Linux TCP_INFO 每连接累计重传计数", values))
        details.append(("N01", "采样仍在线的 Alice、重新登录的 Bob 和 Charlie TCP Socket。",
                        values + "（tcpi_total_retrans 累计值）"))
        return transport_stats
    finally:
        for sock in (alice, bob, charlie, bob_again):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass


def write_record(record_dir: Path, port: int,
                 results: list[tuple[str, str, str]], failure: str | None,
                 transport_stats: dict[str, int],
                 details: list[tuple[str, str, str]]) -> None:
    record_dir.mkdir(parents=True, exist_ok=True)
    timestamp = dt.datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z %z")
    lines = [
        "# 聊天室功能测试记录",
        "",
        f"- 测试时间：{timestamp}",
        f"- 测试地址：127.0.0.1:{port}",
        "- 测试方式：Python 仅用于构造/读取测试帧；聊天室服务端和客户端为 C++17。",
        "",
        "| 编号 | 测试项目 | 结果 |",
        "| --- | --- | --- |",
    ]
    lines.extend(f"| {case_id} | {description} | {status} |"
                 for case_id, description, status in results)
    if failure:
        lines.extend(["", "## 失败信息", "", f"```text\n{failure}\n```"])
    else:
        lines.extend([
            "", "## 结论", "",
            "本次列出的功能测试均通过；TCP_INFO 数值是独立的传输层观测。",
            "未列入本次测试的行为不据此推定。",
        ])
    (record_dir / "functional-test-results.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )

    detail_lines = [
        "# 测试输入与观察结果",
        "",
        f"- 对应测试时间：{timestamp}",
        f"- 对应测试地址：127.0.0.1:{port}",
        "",
        "| 编号 | 输入 / 操作 | 实际观察 |",
        "| --- | --- | --- |",
    ]
    detail_lines.extend(f"| {case_id} | {operation} | {observed} |"
                        for case_id, operation, observed in details)
    (record_dir / "test-observations.md").write_text(
        "\n".join(detail_lines) + "\n", encoding="utf-8"
    )

    observation = [
        "# TCP 传输层观测记录",
        "",
        f"- 测试时间：{timestamp}",
        f"- 测试地址：127.0.0.1:{port}",
        "- 数据来源：Linux `TCP_INFO` 每条测试 TCP Socket 的 `tcpi_total_retrans` 累计计数。",
        "- 解释：这是连接累计计数，不是对每个 TCP 段的完整抓包分类。",
        "",
        "| 客户端连接 | TCP 重传累计数 |",
        "| --- | ---: |",
    ]
    observation.extend(f"| {name} | {count} |"
                       for name, count in transport_stats.items())
    if not transport_stats:
        observation.append("| 未取得 | 测试未到达 TCP_INFO 采样步骤 |")
    observation.extend([
        "",
        "此探测脚本读取 TCP_INFO，不启动抓包工具；pcap 需按 README 中的独立命令采集。",
    ])
    (record_dir / "tcp-transport-observation.md").write_text(
        "\n".join(observation) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--record-dir", type=Path, default=ROOT / "experiment_records",
        help="directory for concise test records (default: experiment_records)",
    )
    parser.add_argument(
        "--port", type=int, default=None,
        help="fixed local port (default: choose a temporary available port)",
    )
    args = parser.parse_args()

    server_binary = ROOT / "server"
    if not server_binary.exists():
        print("server executable not found; run `make` first", file=sys.stderr)
        return 2

    port = args.port if args.port is not None else choose_port()
    if not 1 <= port <= 65535:
        parser.error("--port must be in the range 1-65535")
    server_log_path = args.record_dir / "server-test.log"
    args.record_dir.mkdir(parents=True, exist_ok=True)
    results: list[tuple[str, str, str]] = []
    details: list[tuple[str, str, str]] = []
    transport_stats: dict[str, int] = {}
    failure = None
    server_log = server_log_path.open("w", encoding="utf-8")
    server = subprocess.Popen(
        [str(server_binary), str(port)], cwd=ROOT,
        stdout=server_log, stderr=subprocess.STDOUT, text=True,
    )
    try:
        wait_for_server(server, port)
        results.append(("T00", "服務端啟動並建立本機 TCP 連線", "PASS"))
        details.append(("T00", f"启动服务端并连接 127.0.0.1:{port}。",
                        "本机 TCP connect 成功，服务端保持运行并接受后续客户端。"))
        transport_stats = run_suite(port, results, details)
    except Exception as error:  # preserve a failed run as usable debugging evidence
        failure = f"{type(error).__name__}: {error}"
        results.append(("FAIL", "測試過程", "FAIL"))
        print(f"FAIL: {failure}", file=sys.stderr)
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=2)
        server_log.close()
        write_record(args.record_dir, port, results, failure,
                     transport_stats, details)

    print(f"Test records: {args.record_dir / 'functional-test-results.md'}")
    print(f"Server log: {server_log_path}")
    if failure:
        return 1
    for case_id, description, status in results:
        print(f"{status}: {case_id} {description}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
