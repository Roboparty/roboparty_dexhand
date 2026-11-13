# RP Hand SDK

六轴灵巧手 SDK，提供 C++17、Python 3.9+ 和 Linux ROS 2 接口，共用 C++ 协议与状态管理核心。

[RP1](https://github.com/Roboparty/RP1) · [RP Hand 产品资料](https://github.com/Roboparty/rp_dexhand)

## 快速开始

产品固定速率为 **CAN FD + BRS，1 Mbps / 5 Mbps**，默认节点 ID 为 `1`。
Windows 使用 WinUSB/gs_usb（当前支持 A8FA:8598 单通道适配器），Linux 使用 SocketCAN。

先按 [使用指南](docs/getting-started.md) 准备驱动、编译环境或 Linux CAN 接口，再在仓库根目录安装：

```bash
python -m pip install .
```

Windows（替换实际序列号）：

```powershell
python examples/python/read_state.py --serial YOUR_SERIAL
python examples/python/wave_demo.py --serial YOUR_SERIAL
```

Linux（先退出使用 can0 的程序，再配置 CAN FD 为 1 Mbps / 5 Mbps；重新配置会暂时中断该接口通信）：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details link show can0

python examples/python/read_state.py --interface can0
python examples/python/wave_demo.py --interface can0
```

将 `can0` 替换为实际接口名，确认接口已 UP、启用 FD，且 `bitrate` 为 `1000000`、
`dbitrate` 为 `5000000`。重启或重新插拔后可能需要再次配置，详见[Linux 使用指南](docs/getting-started.md#环境与-can-配置)。

C++ 程序为 `rphand_read_state`、`rphand_wave_demo`，使用相同参数；构建命令见使用指南。
wave 会使能、回零并连续运动，拇指走全行程，按 Ctrl+C 退出并尝试失能。
状态读取不使能、回零或解除急停，但连接会写入反馈配置，不能当作只读 USB 查询。

## Linux 反馈周期实验

独立脚本可读取六轴周期参数、配置反馈上传间隔，并测量实际反馈频率；无需安装 SDK：

```bash
/usr/bin/python3 -B examples/python/feedback.py read-sdo
/usr/bin/python3 -B examples/python/feedback.py set-sdo 20
/usr/bin/python3 -B examples/python/feedback.py read-feedback
/usr/bin/python3 -B examples/python/feedback.py set-feedback 20
```

四个命令分别读取/设置 SDO 参数、测量/设置实际反馈，默认 `can0`、节点 `1`，
可用 `--can can1 --id 2` 修改。`set-sdo 20` 的单位是 ms；加 `--save` 可发送 EEPROM 保存命令，
断电保留仍须重启后验证。`set-feedback 20` 的数值是上传分频，本机约为 20 ms（每组 50 Hz），
设置后自动测量；分频能否保存尚未确认。旧的 `read`/`set` 命令已由这四个独立命令替代。
配置前退出控制程序；SDK 连接会覆盖实验设置。脚本不使能、不回零、不运动。
参数写入读回、单轴设置和实验示例见 [反馈周期实验](docs/feedback.md)。

## Linux OTA 维护

CAN 已由系统配置好时，可直接检测和升级，无需再次启停接口。先停止手部控制程序，
在仓库根目录执行；接口和节点 ID 都必须显式指定：

```bash
/usr/bin/python3 examples/python/ota.py check --interface can0 --node-id 1
/usr/bin/python3 examples/python/ota.py upgrade --interface can0 --node-id 1
```

支持 **DH116S CANFD 左右手，当前设备版本必须 > 1.20**。不传固件路径时，根据设备实际方向
选择仓库随附的左手或右手 V1.22。升级其他版本时，在 `check` 和 `upgrade` 后都加上
`--firmware /path/to/DH116SR-CANFD-APP-H6E001-V1.23.upd`（替换为实际原始文件路径）。
允许同版本重刷，不允许降级；固件版本不限定为 V1.22。

`check` 读取身份、校验所选固件，并进行默认 30 秒通信测试，结束时明确提示是否可以继续升级。
控制器 warning/passive 状态只记录，继续实际通信测试。首次型号查询无应答时会做一次只读 OTA 查询；
OTA 有应答则提示恢复流程，退出码为 2，不触发刷写。
显示 `CHECK PASSED: Ready to upgrade.` 后再运行 `upgrade`，按提示输入包含实际左右手、接口和
节点的确认文字才会开始烧录，例如 `UPGRADE RIGHT can0 1`。
该工具只使用 Python 标准库，不依赖 SDK 编译产物或 ROS。
升级中断后，设备若停留在 bootloader、无法读取应用身份，可用
`upgrade --interface can0 --node-id 1 --recover --firmware <对应设备的原始固件路径>` 从头重刷。
恢复模式需要明确选择左右手固件，不校验原应用版本和序列号；操作步骤见维护指南的恢复章节。
详细流程、升级后查询、日志和支持范围见 [OTA 维护指南](docs/ota.md)。

## 文档

| 文档                             | 内容                                                |
| -------------------------------- | --------------------------------------------------- |
| [使用指南](docs/getting-started.md) | Windows/Linux 安装、示例参数、常见问题              |
| [SDK 功能手册](docs/sdk-manual.md)  | 核心类型、控制流程、C++/Python/ROS 2 接口和故障排查 |
| [API](docs/api.md)                  | C++/Python 接入、单位、状态和失败语义               |
| [ROS 2](docs/ros2.md)               | Jazzy 节点、话题、服务与 launch                     |
| [OTA 维护](docs/ota.md)             | Linux 指定接口和节点的检测、固件升级与复查          |
| [架构与开发](docs/architecture.md)  | 内部分层、构建选项和测试                            |

公共头文件在 `include/rphand/`，核心实现在 `src/`，语言和框架接入在 `python/`、`ros2/`，
可运行示例在 `examples/cpp/`、`examples/python/`。

## 许可

当前仓库未附带单独许可证文件。如需在组织或公开项目中分发，请先确认项目许可证和设备协议的授权范围。
