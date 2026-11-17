# 实验二：Npcap 以太网数据帧捕获

这是一个在 WSL 中使用 MinGW-w64 交叉编译、并在 Windows Npcap 驱动上运行的命令行程序。程序枚举当前可用设备，按本次列表中的编号选择设备，检查链路类型，然后捕获数据帧并显示以太网头部字段。默认不写入抓包文件，也不启用混杂模式。

## 环境前提

- Windows 已安装 Npcap，并允许当前账户打开所选捕获设备。
- WSL 已安装 `x86_64-w64-mingw32-g++`、GNU Make 和 `file`。
- Npcap SDK 1.17 位于仓库旁的 `../tools/npcap-sdk-1.17/`。构建从该位置读取头文件和 `Lib/x64/wpcap.lib`，不会复制 SDK。

## 构建

在 WSL Bash 中进入 `workspace/` 后运行：

```bash
cd ~/projects/network-lab/workspace
make
file build/lab2/npcap_capture.exe
```

如果 SDK 不在默认路径，可显式指定：

```bash
make SDK_DIR=/path/to/npcap-sdk-1.17
```

清理生成物：

```bash
make clean
```

## 在 WSL Bash 中运行 Windows 程序

以下命令假设当前目录是 `workspace/`，并通过 PowerShell 调用 Windows Npcap：

```bash
EXE_WIN="$(wslpath -w "$PWD/build/lab2/npcap_capture.exe")"
powershell.exe -NoProfile -Command \
  "\$env:Path = 'C:\Windows\System32\Npcap;' + \$env:Path; & '$EXE_WIN' --list; Write-Output ('ExitCode=' + \$LASTEXITCODE)"
```

先用 `--list` 查看本次运行枚举到的设备，再按刚显示的编号启动有限捕获。下面的 `N` 必须替换为本次列表里的编号；设备顺序可能变化：

```bash
powershell.exe -NoProfile -Command \
  "\$env:Path = 'C:\Windows\System32\Npcap;' + \$env:Path; & '$EXE_WIN' --device-index N --count 20; Write-Output ('ExitCode=' + \$LASTEXITCODE)"
```

也可以直接运行程序并交互式选择设备：

```bash
powershell.exe -NoProfile -Command \
  "\$env:Path = 'C:\Windows\System32\Npcap;' + \$env:Path; & '$EXE_WIN'"
```

不指定 `--count` 时程序持续捕获，按 `Ctrl+C` 结束。`--count N` 中的 N 是成功解析并显示的帧数；短于 15 字节的帧会被跳过并单独提示，不计入 N。

参数错误时程序显示用法。`--device-index` 和 `--count` 都要求正整数；`--list` 只用于枚举，不与捕获参数组合。

## 输出与解析规则

每条有效帧的输出包括编号、时间、源 MAC、目的 MAC、Type/Length、Type/Length 后第一个字节以及捕获长度。解析固定读取：

| 字段 | 偏移 | 长度 |
| --- | ---: | ---: |
| 目的 MAC 地址 | 0 | 6 字节 |
| 源 MAC 地址 | 6 | 6 字节 |
| Type/Length | 12 | 2 字节，大端序 |
| 紧随 Type/Length 的字节 | 14 | 1 字节 |

解析器按 `pcap_pkthdr::caplen` 检查至少 15 字节。即使 Type/Length 是 VLAN 标签，后续字节仍严格取偏移 14，符合本实验要求。

## 源码模块

- `src/main.cpp`：命令行参数、设备选择与程序入口。
- `src/device.cpp`、`src/device.h`：调用 `pcap_findalldevs()`，显示设备列表并按本次列表编号查找设备。
- `src/capture.cpp`、`src/capture.h`：调用 `pcap_open_live()`、`pcap_datalink()` 和 `pcap_next_ex()` 执行捕获；使用 RAII 关闭捕获句柄。
- `src/ethernet.cpp`、`src/ethernet.h`：不依赖 Npcap 的以太网字段解析与 MAC 格式化。
- `tests/ethernet_test.cpp`：使用固定字节序列检查字段偏移、字节序、MAC 格式及短帧边界。

主要执行顺序：枚举设备并显示 → 选择设备 → 打开设备 → 检查 `DLT_EN10MB` → 循环读取数据包 → 校验捕获长度并解析/显示 → 关闭句柄、释放设备列表。

## 解析器验证

在 `workspace/` 运行：

```bash
make test
```

用例检查已知的 15 字节帧，以及 0、14、15 字节长度边界和空指针输入。该测试只验证解析器，不代表 Windows Npcap 实时捕获已通过。

## 错误处理

- 枚举失败：输出 Npcap 错误信息并返回非零状态；空设备列表会明确提示。
- 非法设备编号：说明当前有效范围，不会打开错误设备。
- 打开设备失败：显示 `pcap_open_live()` 的错误信息。
- 非 `DLT_EN10MB` 设备：显示实际链路类型并停止，不解析成以太网 MAC 地址。
- 捕获超时：继续等待；捕获错误显示 `pcap_geterr()`；捕获源结束时正常退出。
- 短帧：使用捕获长度而非原始长度检查，跳过且不读取越界字节。

若 Windows 提示找不到 `wpcap.dll`，确认 `C:\Windows\System32\Npcap` 在进程 `PATH` 中且 Npcap 已安装。若设备无法打开，检查设备编号是否来自最新列表、Npcap 驱动状态和账户权限；回环设备可能使用非 Ethernet 链路类型，不适用于当前解析器。

## 当前验收记录

本次在 2026-11-03 完成的验证：

| 项目 | 命令/证据 | 结果 |
| --- | --- | --- |
| Windows x64 交叉编译 | 2026-11-17 执行 `make` | 退出码 0 |
| 可执行文件格式 | `file build/lab2/npcap_capture.exe` | `PE32+ executable (console) x86-64, for MS Windows` |
| 解析器固定字节与边界检查 | 2026-11-03 在目录调整前执行 `make -C lab2 test` | 退出码 0，输出 `Ethernet parser checks passed.` |
| Windows Npcap 枚举和实时捕获 | 通过 `powershell.exe` 调用程序 | 尚未验证：WSL 在启动 Windows 进程前返回 `UtilBindVsockAnyPort:309: socket failed 1`，程序未运行 |
| Wireshark 对照与实验截图 | 需观察 Windows 实际抓包 | 尚未完成，不能把示例帧当作运行证据 |

因此当前已验证交叉编译和独立解析逻辑；设备枚举、打开、实时捕获、四字段真实输出、Wireshark 对照，以及正式实验报告仍需在 Windows 终端环境可正常启动该程序后补齐。建议使用本节“在 WSL Bash 中运行 Windows 程序”的命令重试，并保存设备列表与有限捕获的真实终端记录。
