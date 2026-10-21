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

Linux（先配置 can0 为 1 Mbps / 5 Mbps）：

```bash
python examples/python/read_state.py --interface can0
python examples/python/wave_demo.py --interface can0
```

C++ 程序为 `rphand_read_state`、`rphand_wave_demo`，使用相同参数；构建命令见使用指南。
wave 会使能、回零并连续运动，拇指走全行程，按 Ctrl+C 退出并尝试失能。
状态读取不使能或回零，但连接会配置反馈并可能恢复遗留急停，不能当作只读 USB 查询。

## 文档

| 文档 | 内容 |
| --- | --- |
| [使用指南](docs/getting-started.md) | Windows/Linux 安装、示例参数、常见问题 |
| [API](docs/api.md) | C++/Python 接入、单位、状态和失败语义 |
| [ROS 2](docs/ros2.md) | Jazzy 节点、话题、服务与 launch |
| [架构与开发](docs/architecture.md) | 内部分层、构建选项和测试 |

公共头文件在 `include/rphand/`，核心实现在 `src/`，语言和框架接入在 `python/`、`ros2/`，
可运行示例在 `examples/cpp/`、`examples/python/`。

## 许可

当前仓库未附带单独许可证文件。如需在组织或公开项目中分发，请先确认项目许可证和设备协议的授权范围。
