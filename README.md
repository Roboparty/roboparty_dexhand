# roboparty_dexhand_sdk

用于 DH116S 六轴灵巧手的 Linux CAN FD SDK，提供 C++17、Python 和 ROS 2 接口。底层通信与动作逻辑由同一 C++ 核心实现，Python 使用 pybind11 绑定，ROS 驱动直接链接核心。

支持连接、使能、失能、停止、整手回零、单轴/六轴位置控制、运动参数设置及反馈读取。项目携带 LHandProLib 2026-09-21 厂家库。

## 支持范围

| 项目 | 支持范围 |
| --- | --- |
| 设备 | DH116S，六个主动通道 |
| 通信 | Linux SocketCAN CAN FD；系统须预先配置接口 |
| 架构 | x86_64、aarch64/ARM64 |
| C++ | C++17，CMake >= 3.16 |
| Python | Python >= 3.9，绑定需按目标架构和 Python ABI 编译 |
| ROS | ROS 2 Jazzy；统一脚本使用 /opt/ros/jazzy |
| 参考环境 | Ubuntu 24.04 ARM64 + Jazzy；本地 x86_64 C++/Python 构建已验证 |

不提供故障清除、触觉数据、力控/阻抗控制或轨迹规划。其他手型、操作系统和 ROS 版本不在当前验证范围内。

## 编译

在仓库根目录运行，只安装需要使用的入口依赖：

```bash
# C++
sudo apt-get install build-essential cmake iproute2 can-utils
# Python 额外依赖
sudo apt-get install python3-dev pybind11-dev
# ROS 额外依赖：已有 Jazzy 环境下
sudo apt-get install python3-colcon-common-extensions ros-jazzy-ament-cmake \
  ros-jazzy-rclcpp ros-jazzy-rclpy ros-jazzy-rcl-interfaces \
  ros-jazzy-std-msgs ros-jazzy-std-srvs ros-jazzy-sensor-msgs \
  ros-jazzy-rosidl-default-generators ros-jazzy-rosidl-default-runtime \
  ros-jazzy-launch-ros ros-jazzy-ament-index-python
```

```bash
bash build.sh cpp
bash build.sh python
bash build.sh ros
# 或依次构建全部入口
bash build.sh all
```

| 入口 | 构建目录 | 安装目录 |
| --- | --- | --- |
| C++ | build/cpp | install/cpp |
| Python | build/python | install/python |
| ROS | build/ros | install/ros |

构建不会自动连接设备或执行运动。修改底层后，使用 `bash build.sh all` 更新三种产物。手动 CMake、pip、交叉编译和外部项目接入见 [构建指南](docs/build.md)。

## 硬件准备

先确认供电、接线、终端电阻以及设备的 CAN 参数。下面是仲裁 1 Mbps、数据 5 Mbps 的配置示例，接口名和速率必须与实际设备一致；会中断该接口已有通信：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details -statistics link show can0
```

双手示例默认左手 `can0`、右手 `can3`，节点 ID 均为 1；请按实际接线修改，并配置两条接口。这些名称不代表自动识别左右手。同一总线可以使用两个不同节点 ID，不能使用相同 ID。

## C++ 快速使用

应用包含 `<dh116s/hand.hpp>`，链接 `roboparty::dexhand_sdk`。以下仅连接并读取反馈，不使能或回零；连接可能为恢复遗留急停而失能、撤销急停：

```cpp
#include <dh116s/hand.hpp>
#include <iostream>

int main() {
    dh116s::Hand hand;
    if (!hand.connect("can0", 1)) return 1;
    const auto state = hand.get_state();
    std::cout << "feedback_valid=" << state.feedback_valid << '\n';
    hand.disconnect();
    return state.feedback_valid ? 0 : 1;
}
```

## Python 快速使用

```bash
source setup_python.sh
python3 - <<'PY'
from roboparty_dexhand_sdk import Hand

hand = Hand()
try:
    if not hand.connect("can0", 1):
        raise RuntimeError("connect failed")
    state = hand.get_state()
    print(state.feedback_valid, state.positions, state.fault_codes)
finally:
    hand.disconnect()
PY
```

这个例子同样只检查连接与反馈。C++/Python 的完整接口、运动流程和清理约定见 [API 文档](docs/api.md)。

## ROS 快速使用

终端 1，在仓库根目录启动双手驱动：

```bash
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py command_rate_hz:=200
```

节点启动后等待 reset，不自动连接或回零。终端 2 加载相同环境后操作左手：

```bash
# 会使能并驱动整手回零；先清空运动空间
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
ros2 topic echo /dexhand_left/state
# 观察到请求后的 phase=5 且 feedback_valid=true 后，Ctrl+C 退出查看
ros2 topic pub --once /dexhand_left/cmd std_msgs/msg/Int32MultiArray \
  '{data: [0, 0, 1000, 1000, 1000, 1000]}'
ros2 service call /dexhand_left/stop std_srvs/srv/Trigger '{}'
```

右手将 `dexhand_left` 替换为 `dexhand_right`。Service 返回成功仅表示接受请求，完成状态看 `state`。详细命令、参数和手柄适配见 [ROS 指南](docs/ros2.md)。

## Wave Demo

`examples/` 只保留 C++ 和 Python 两个双手示例，通过参数选择频率：

```bash
# 不打开 CAN，不运动；Python dry-run 无需安装绑定
./install/cpp/bin/dh116s_wave_demo --rate 50 --dry-run
python3 examples/wave_demo.py --rate 150 --dry-run

# 真实双手运动：先退出 ROS 驱动及其他控制程序
./install/cpp/bin/dh116s_wave_demo --rate 200
source setup_python.sh
python3 examples/wave_demo.py --rate 50
```

默认速度 20000、限流 400、四指幅度 6000、周期 3 秒、10 个周期，拇指在波浪阶段保持目标 0。支持 `--rate 50/100/150/200`、`--left CAN NODE`、`--right CAN NODE`、`--speed`、`--current`、`--amplitude`、`--period-ms`、`--cycles`。完整参数可用 `--help` 查看。

真实 demo 会连接、使能、整手回零、波浪、返回零位，最后失能并断开。C++ 额外提供 `--connect-only`；它会配置反馈并可能恢复遗留急停，不是纯只读操作。ROS 单手波浪使用独立的 [wave.py](docs/ros2.md)。

## 必须了解的行为

- 六轴顺序：拇指侧摆、拇指屈伸、食指、中指、无名指、小指。位置范围 0..10000，为行程当量，不是弧度。
- 设置位置成功不等于设备运动或到位；读取反馈必须检查 `feedback_valid`。
- `disconnect()` 只释放本地资源，不自动停止或失能；`stop()` 不保证保持抓握。停止后按完整初始化流程恢复。
- ROS 会持续重发最后目标，上层退出或停止发布不会自动 stop。每只手只应由一个控制端管理，SDK 不做跨进程独占。

## 目录与文档

```text
include/dh116s/    C++ 公共接口
src/              C++ 核心实现
python/           Python 绑定和类型提示
ros2/             ROS 包、消息、launch、控制脚本
examples/         wave_demo.cpp、wave_demo.py
cmake/            安装导出和交叉编译配置
third_party/      厂家头文件和二进制库
tests/            可选无硬件检查
docs/             正式使用文档
```

- [API](docs/api.md)：接口、单位、反馈、线程与生命周期。
- [ROS 2](docs/ros2.md)：控制命令、参数、状态机、RP1 手柄适配。
- [构建](docs/build.md)：依赖、安装、集成、交叉编译和可选检查。
- [排查](docs/troubleshooting.md)：已知限制、常见问题、日志与抓包。
