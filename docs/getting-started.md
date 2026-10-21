# 使用指南

SDK 支持 C++17、Python 3.9+，产品固定使用 **CAN FD + BRS，仲裁段 1 Mbps、数据段 5 Mbps**，
默认节点 ID 为 `1`。以下命令均在仓库根目录执行。

先运行状态读取，再运行运动示例；wave 会使能、回零并连续运动，按 Ctrl+C 退出并尝试失能。
`read_state` 不使能、不回零，但连接会配置反馈，并可能恢复遗留软件急停，不保证保持原有抓握。

## Windows

### 环境

- 当前支持 `A8FA:8598`、兼容 gs_usb 的单通道 CAN FD 适配器。设备管理器中应使用 WinUSB 驱动并正常启动。
- 源码构建需要 Visual Studio 2022“使用 C++ 的桌面开发”（含 Windows SDK）、CMake 3.20+；Python 使用 64 位版本。
- 在设备属性的“详细信息 → 设备实例路径”中确认实际序列号，替换下文的 `YOUR_SERIAL`。
- SDK 自动配置产品速率，无需在运行命令中传波特率。

### C++

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel 2
cmake --install build/windows --config Release --prefix install/windows
.\install\windows\bin\rphand_read_state.exe --serial YOUR_SERIAL
.\install\windows\bin\rphand_wave_demo.exe --serial YOUR_SERIAL
```

仅检查 USB 访问可运行 `rphand_usb_probe.exe YOUR_SERIAL`（同一 `bin` 目录）。
probe 不配置或启动 CAN、不发送 CAN 帧；成功不代表灵巧手通信正常。

### Python

在 **Developer PowerShell for VS 2022** 中从源码安装，让编译器和 Windows SDK 进入终端环境：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install .
.\.venv\Scripts\python.exe -X utf8 examples/python/read_state.py --serial YOUR_SERIAL
.\.venv\Scripts\python.exe -X utf8 examples/python/wave_demo.py --serial YOUR_SERIAL
```

如果已有匹配 Python 版本与架构的 wheel，可用 `python -m pip install <wheel路径>` 安装。
同一 USB 适配器只能被一个会话占用，切换 C++/Python 前先退出原程序。

## Linux

### 环境与 CAN 配置

需要支持 CAN FD 的适配器及内核驱动、C++17 编译器、CMake 3.20+。
Python 源码安装还需要对应解释器的开发头文件。
先退出使用 CAN 的程序，再配置实际接口（以下为 `can0`）：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details -statistics link show can0
```

确认 UP、FD 和两段速率正确。SDK 不执行 sudo、不创建接口、不修改系统速率；
重启或重新插拔后可能需要再次配置。Windows 的速率默认值不能替代这一步。

### C++

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
cmake --install build/linux --prefix install/linux
./install/linux/bin/rphand_read_state --interface can0
./install/linux/bin/rphand_wave_demo --interface can0
```

### Python

```bash
python3 -m venv .venv
.venv/bin/python -m pip install .
.venv/bin/python examples/python/read_state.py --interface can0
.venv/bin/python examples/python/wave_demo.py --interface can0
```

源码安装自动准备 pybind11 和 scikit-build-core；缺少 `Python.h` 时安装匹配版本的开发包。
始终使用安装 SDK 的同一个 Python。原 `build.sh` 工作流仍支持，见 [开发说明](architecture.md)。

## 示例参数与行为

C++ 源码位于 `examples/cpp/`，Python 位于 `examples/python/`，参数保持一致：

| 参数 | 默认值 / 用途 |
| --- | --- |
| `--serial SERIAL` | Windows 必填，选择 USB 适配器 |
| `--interface NAME` | Linux 默认 `can0` |
| `--node-id ID` | 默认 `1`，范围 `1..127` |
| `--amplitude VALUE` | wave 四指幅度，默认 `6000`，范围 `1..10000` |
| `--connect-only` | wave 只检查反馈 5 秒，退出时尝试失能，不使能、回零或执行动作 |
| `--help` | 查看说明，不访问设备 |

wave 初始化顺序为：连接、失能、速度 16000、电流限制 400、力矩到位不停、使能、回零。
之后以 50 Hz 循环发送目标：拇指侧摆全行程往返 2 秒、拇指屈伸全行程往返 2 秒、四指 wave 6 秒。
拇指始终使用 `0..10000` 全行程，幅度参数只影响四指。动作按时间推进，不等待到位。
单位、控制方法与失败语义见 [API](api.md)。

`read_state` 打印反馈有效性、位置与故障码后断开；反馈无效或故障码非零时退出失败。
wave 反馈异常或发送失败时也退出，并尝试失能。退出码：正常 `0`、失败 `1`、参数错误 `2`、
Ctrl+C 通常为 `130`；清理是否成功应同时看日志。断开或强制结束进程不能保证设备已停止。

兼容入口保留：`python examples/wave_demo.py` 转发到新目录；Linux 的 `--side left|right`
映射 `can0|can3`，不能与 `--interface` 混用，也不用于 Windows。
Windows 的 `--bitrate` / `--data-bitrate` 可高级覆盖，普通产品无需设置。
双手使用不同 Windows 适配器序列号，或不同 Linux 接口/节点组合；避免多个程序控制同一设备。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| Windows 无法打开设备 | WinUSB 驱动、支持型号、实际序列号、其他程序占用 |
| Linux 接口不存在或未启动 | 实际接口名、驱动、UP/FD 状态、系统两段速率 |
| probe 通过但 connect 失败 | 灵巧手供电、CAN 接线、终端电阻、节点 ID |
| 反馈超时、ACK 错误、bus-off | 首次错误日志及物理总线；不要持续重发运动指令 |
| wave 退出或失能未确认 | 失败发生在哪一步、反馈有效性、故障码和设备实际状态 |
| Python 找不到包或默认值仍旧 | 用运行解释器执行 `python -m pip show rphand`；修改核心后重新安装 |

排查连接时给 wave 增加 `--connect-only`。正常连接会检查两组反馈约为 50 Hz。
Windows 保存日志可在命令末尾加 `2>&1 | Tee-Object connection.log`；使用 `-X utf8` 时先执行
`[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)`。
Linux 可加 `2>&1 | tee connection.log`。
报告问题附操作系统、适配器型号、SDK 版本/提交、完整命令及首次失败日志，可隐藏序列号。
