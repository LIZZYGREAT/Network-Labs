# 实验二：Npcap 数据帧捕获程序——Codex 执行方案

## 一、执行目标与现有前提

在**当前 `lab2` 分支**上，实现一个基于 Npcap 的 Windows 命令行数据帧捕获工具。开发和构建从 WSL 进行，通过 MinGW-w64 生成 Windows x64 `.exe`，在 Windows 侧加载现有 Npcap 1.88 运行库，枚举网卡、选择设备、捕获数据帧并显示指定四个字段。

**以 `~/projects/network-lab/workspace/` 作为本次任务的仓库工作目录。** `tools/npcap-sdk-1.17/` 位于其上一级 `~/projects/network-lab/tools/`，只作为外部构建依赖使用。不要把源码、构建产物或报告新建在仓库外层的 `~/projects/network-lab/lab2/` 中，也不要移动 SDK。

已完成的先导验证：

- `x86_64-w64-mingw32-g++` 可交叉编译 Windows x64 程序。
- SDK 1.17 的头文件、`Lib/x64/wpcap.lib` 已用于链接成功。
- 为避免 Windows 缺少 MinGW C++ 运行时，编译选项使用 `-static -static-libgcc -static-libstdc++`。
- Windows 中 `C:\Windows\System32\Npcap\wpcap.dll` 存在。
- `pcap_lib_version()` 返回 Npcap **1.88**，`pcap_findalldevs()` 列出 **11** 个设备，退出码为 **0**。
- 已观察到的无线网卡描述为 `Realtek 8822CE Wireless LAN 802.11ac PCI-E NIC`（先前编号 4；不要硬编码编号或设备名称）。

本次的关键新增任务是：**网卡打开、链路层检查、实时捕获、指定字段解析、可靠退出、完整运行验证**。

## 二、工作边界与 Git 约束

1. **先检查现状再修改**：从 `workspace/` 执行 `git status --short --branch`、`git branch --show-current`、`git log -1 --oneline`，确认当前在 `lab2`；若不在此分支，不自动切换、重置或覆盖用户工作，先说明现状。
2. 查看 `workspace/` 中原有 `client`、`server`、`README.md`、`.gitignore` 和已有构建配置，了解实验一结构。**原实验一代码保持原样**；不得为了本次任务重写、迁移或清理它们。
3. 新实现放在 `workspace/lab2/`（如存在同名目录，先检查并在现有结构中合理延续），仅在确有必要时补充仓库级说明或 `.gitignore`。
4. **不得修改、复制入仓库或提交** `../tools/npcap-sdk-1.17/`；SDK 路径通过构建变量引用。
5. 不创建无关框架，不引入额外运行依赖，不执行 `git reset --hard`、`git clean -fd`、强制推送等破坏性操作。保留所有已有未提交修改。
6. 提交之前检查 diff，确保只包含本实验相关源码、构建脚本和说明；不要提交 `.exe`、`.o`、临时日志、大量抓包文件和 SDK 文件。**未经用户明确要求，不自动提交或推送。**

## 三、推荐目录和模块职责

在 `workspace/` 下建议使用：

```text
workspace/                        # Git 工作目录（当前 lab2 分支）
├── client                        # 实验一，保持不动
├── server                        # 实验一，保持不动
├── README.md                     # 原项目说明；如需要仅补充实验二入口
├── .gitignore
└── lab2/
    ├── src/
    │   ├── main.cpp              # 参数解析、主流程与退出状态
    │   ├── device.h / device.cpp # 设备枚举、显示、选择
    │   ├── capture.h / capture.cpp # 打开网卡、捕获循环、句柄管理
    │   └── ethernet.h / ethernet.cpp # 字节解析与显示格式化
    ├── tests/
    │   └── ...                   # 可选：不依赖实时网卡的解析测试
    ├── build/                    # 构建产物；加入忽略规则
    ├── Makefile                  # 统一交叉编译入口
    └── README.md                 # 使用、编译、运行、常见问题及验收
```

可以合并少量文件以避免过度拆分，但至少要做到**捕获 API 调用和以太网帧解析相互分离**。所有文件名与最终设计在 `lab2/README.md` 中保持一致。

## 四、核心设计与实现约束

### 1. 程序启动、设备枚举与设备选择

使用 `pcap_findalldevs()` 枚举设备；显示用户可辨认的设备描述，并保留内部的设备名称供 `pcap_open_live()` 使用。输入必须验证是否为合法的正整数以及是否在当前列表范围内。

推荐同时支持：

- 默认运行：显示网卡列表，交互式输入编号。
- `--list`：仅枚举并显示设备，便于非交互检查。
- `--device-index N`：使用**当前一次枚举列表**中的第 N 项，便于 WSL 调用 Windows 程序时指定设备。
- `--count N`：达到 N 个成功解析/显示的帧后结束，便于有限测试；如对不支持或过短的帧计数策略不同，须在说明中写清楚。

命令行参数的错误使用须提示用法。设备列表应在释放前完成设备打开，避免保留悬空指针。

### 2. 打开网卡与链路层校验

使用 `pcap_open_live()` 打开选中设备，建议初始设置：

- `snaplen = 65535`：提供足够大的捕获字节上限。
- `promisc = 0`：本实验只需捕获本机相关流量，不默认开启混杂模式。
- `to_ms = 1000`：捕获读取的等待超时参数，具体行为以实际平台为准。

打开失败则显示 Npcap 错误信息并正常释放资源。打开成功后调用 `pcap_datalink()`：

- 如果等于 `DLT_EN10MB`，继续按照标准以太网头部解析。
- 如果不是，显示实际链路层类型并提示更换网卡，结束本次捕获；**不可假定回环接口具有以太网 MAC 头部**。

### 3. 实时捕获循环

使用 `pcap_next_ex()` 取得 `pcap_pkthdr` 与原始数据指针，明确处理各类返回值：

| 返回值 | 处理方式 |
| --- | --- |
| `1` | 有数据包；验证指针与捕获长度，然后解析、输出 |
| `0` | 读取等待超时；继续循环，不将其视为失败 |
| `-1` | 捕获错误；打印 `pcap_geterr()` 并退出 |
| `-2` | 捕获结束；正常结束循环（尽管实时设备一般不常用到） |

退出时关闭 `pcap_t*` 并释放枚举列表；建议使用 RAII 或明确统一的清理路径。支持按最大帧数退出；如加入 Ctrl+C 优雅退出处理，应确保 Windows 实际运行验证，不以未测试的信号处理方案替代有限帧数验收。

### 4. 以太网帧字段解析

解析函数只接收原始字节、已捕获长度，返回解析结果或错误原因，**不直接调用 Npcap API**。

标准以太网帧开头 15 字节的关键布局为：

```text
byte  0.. 5 : Destination MAC (6B)
byte  6..11 : Source MAC      (6B)
byte 12..13 : Type/Length     (2B, big-endian)
byte 14     : First byte immediately after Type/Length
```

处理规则：

1. `header->caplen < 15` 时跳过并计入跳过次数，不越界读取；判断依据是**捕获长度 `caplen`**，而不是原始帧总长度 `len`。
2. 复制目的 MAC 和源 MAC 各 6 字节，使用 `XX:XX:XX:XX:XX:XX` 格式打印。
3. 计算字段值：`(uint16_t(packet[12]) << 8) | uint16_t(packet[13])`。
4. 读取 `packet[14]`，输出两位十六进制 `0xXX`。
5. 四项字段必须在每条记录中明确命名或有清晰表头。

对于常规 Ethernet II 帧，`0x0800`、`0x0806`、`0x86DD` 分别是常见的 IPv4、ARP、IPv6 EtherType。注意题目要求的是**紧接偏移 12～13 字段之后的第一个字节**，始终优先读取偏移 14；遇到 802.1Q VLAN 标记等情况，这个字节未必是 IP 头部首字节，**不得擅自跳过 VLAN 头改变题目要求的读取位置**。

### 5. 输出格式

建议终端按行输出，例如（纯示例，不代表实际捕获）：

```text
#    Time          Source MAC         Destination MAC    Type/Length  Next byte  Captured
1    14:20:01.123  02:11:22:33:44:55 00:AA:BB:CC:DD:EE 0x0800       0x45       74
2    14:20:02.010  00:AA:BB:CC:DD:EE FF:FF:FF:FF:FF:FF 0x0806       0x00       42
```

实际列宽须按数据合理对齐；可以只显示帧编号、四个必显字段及捕获长度，避免为了美观增加不必要的功能。

## 五、执行阶段与验收门禁

### 阶段 A：检查仓库与复现构建

- 确认 `workspace/` 为 Git 工作区、当前分支为 `lab2`，记录 `git status`。
- 检查实验一现有内容，识别应该保留的文件。
- 确认 `../tools/npcap-sdk-1.17/Include/pcap.h` 和 `../tools/npcap-sdk-1.17/Lib/x64/wpcap.lib` 存在。
- 创建 `workspace/lab2/` 新实验目录并编写最小 Makefile，封装 SDK 引用路径与静态 MinGW 运行时链接参数。

**验收：** 编译输出 Windows x64 PE `.exe`；不需要复制 SDK 到源码目录；原实验一文件没有实质变动。

### 阶段 B：设备管理与实时捕获

- 枚举、选择网卡；打开设备，检查 `DLT_EN10MB`。
- 循环调用 `pcap_next_ex()`，最初只打印帧编号、捕获长度与时间戳，确认真实抓包正常。
- 以当前实际枚举列表确认 Realtek 物理无线网卡的编号；不要把历史的“4”写死。

**验收：** 真实抓包得到至少数条数据帧；超时和错误返回值正确处理；可正常退出并释放资源。

### 阶段 C：帧解析与边界测试

- 独立实现以太网帧解析，接入捕获循环。
- 使用构造的固定字节数组检查目的 MAC、源 MAC、字节序、偏移 14 的字节值。
- 测试 `caplen = 0`、`14`、`15`，确认不足 15 字节不会越界。
- 测试选择回环设备或其他非 `DLT_EN10MB` 设备时的行为（如果可用），确保不产生伪造 MAC 地址。

**验收：** 四个必显字段准确打印，已有边界测试通过，解析模块没有对 Npcap 的隐式依赖。

### 阶段 D：运行验证与文档交付

- 真实网卡运行 `--count` 有限捕获，并记录命令、退出码、终端实际输出；可以使用浏览器访问或 `ping` 产生网络活动。
- 使用 Wireshark 对比至少一份能够可靠关联的帧：核对 MAC、Type/Length、偏移 14 的值。
- 编写 `lab2/README.md`：安装前提、构建命令、运行命令、设备选择、常见故障、关键 API、简要模块关系。
- 输出最终验收清单，明确已实际执行与尚需人工补充截图的事项。**本轮任务以可运行代码和使用说明为主；正式 LaTeX 报告可在截图齐备后另行编写。**

**验收：** 从干净构建目录可以重复生成可执行文件；Windows 运行并输出真实帧字段；仓库文档与现状一致。

## 六、构建与运行的预期入口

以下是**期望由 Codex 实现并维护的标准入口**，不是声称当前目录下已存在这些文件。

在 WSL：

```bash
cd ~/projects/network-lab/workspace
make -C lab2
file lab2/build/npcap_capture.exe
```

预期：`file` 输出包含 `PE32+ executable (console) x86-64, for MS Windows`。

运行 Windows 程序（仍然在 WSL Bash 中执行）：

```bash
cd ~/projects/network-lab/workspace
EXE_WIN="$(wslpath -w "$PWD/lab2/build/npcap_capture.exe")"
powershell.exe -NoProfile -Command \
  "\$env:Path = 'C:\Windows\System32\Npcap;' + \$env:Path; & '$EXE_WIN' --list; Write-Output ('ExitCode=' + \$LASTEXITCODE)"
```

预期：显示 Windows Npcap 设备列表并正常退出。实际捕获前以最新列表确认设备编号；例如设备仍为编号 4，可使用：

```bash
powershell.exe -NoProfile -Command \
  "\$env:Path = 'C:\Windows\System32\Npcap;' + \$env:Path; & '$EXE_WIN' --device-index 4 --count 20; Write-Output ('ExitCode=' + \$LASTEXITCODE)"
```

预期：输出真实捕获帧的四项字段，达到约定数量后退出并显示 `ExitCode=0`。如果捕获不到帧，应检查真实网络流量、网卡选择、Npcap 权限和链路类型，不得捏造成功结果。

**注意：** 如果用户从 Windows PowerShell 而不是 WSL Bash 启动，上述带 `\` 的 Bash 续行和 `wslpath` 指令不能原样使用。README 应分别注明命令适用的终端；可额外提供可直接从 Windows PowerShell 执行的命令。

## 七、必须提交的实现说明与结果

完成代码工作后向用户报告：

1. 最终修改/新增文件清单、各文件的实际作用。
2. 实际使用的主要 Npcap API 及执行顺序。
3. 已执行的构建命令、真实运行命令与退出结果。
4. 解析验证方式及具体通过/失败情况。
5. 实验原始六项要求的逐项完成度。
6. 尚需用户提供的截图或人工操作；不得用示例输出代替实际结果。
7. 最终 `git status --short` 与变更范围检查；不自动提交。

## 八、完成标准

能从 `workspace/lab2/` 独立构建 Windows `.exe`；在 Windows Npcap 上枚举并打开真实网卡；捕获 Ethernet 格式数据帧；按要求显示源 MAC、目的 MAC、类型/长度字段、紧随其后的首字节；正确处理短帧和不支持的链路类型；完成真实运行和解析验证。原 `workspace/client`、`workspace/server` 不受影响，`../tools/npcap-sdk-1.17` 未被移动、复制或提交。
