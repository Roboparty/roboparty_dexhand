# rphand 灵巧手 SDK

`rphand` 是面向 rphand 六轴灵巧手的 Linux SDK，通过 SocketCAN CAN FD 提供 C++、Python 和 ROS 2 接口。

SDK 的 C++、Python 和 ROS 2 接口共用同一套设备通信和状态管理实现，支持连接、使能、失能、停止、回零、位置控制、速度与电流设置，以及反馈状态读取。

> 设备运动具有实际风险。首次接入前请确认 CAN 接口、节点 ID、机械范围和急停措施，并先使用无硬件测试验证程序流程。

## 特性

- C++17 API：`rphand::Hand`
- Python API：`rphand.Hand`
- ROS 2 Jazzy 驱动、双手 launch、命令和 wave 示例
- Linux SocketCAN CAN FD 通信
- 六轴位置、速度、电流、使能、回零、力矩到位和故障反馈
- 无硬件单元测试、CMake 安装包和 Python 类型声明

## 目录

```text
include/                       C++ 公共头文件
src/                           C++ SDK 和 CAN 协议实现
python/                        Python 绑定和类型声明
ros2/rphand_ros/   ROS 2 包、节点、消息和示例
examples/                      C++ / Python wave 示例
tests/                         无硬件测试
docs/                          API、架构和 ROS 2 详细文档
```

## 环境要求

- Linux，CMake 3.16 及以上
- C++17 编译器
- Python 3.9 及以上（构建 Python 绑定还需要 Python 开发文件和 pybind11）
- ROS 2 Jazzy 和 colcon（仅构建 ROS 2 包时需要）

## 快速构建

在仓库根目录执行：

```bash
# 构建并安装 C++ SDK 和 wave 示例
bash build.sh cpp

# 构建 Python 绑定；构建和运行使用同一个解释器
RPHAND_PYTHON=/usr/bin/python3 bash build.sh python
RPHAND_PYTHON=/usr/bin/python3 source setup_python.sh
/usr/bin/python3 -c 'from rphand import Hand; print(Hand)'

# 构建 ROS 2 包
bash build.sh ros

# 一次构建全部组件
bash build.sh all
```

构建产物分别位于 `install/cpp`、`install/python` 和 `install/ros`。可通过 `RPHAND_BUILD_JOBS` 设置并行编译数，通过 `RPHAND_PYTHON` 指定 Python 解释器。Python 绑定必须使用与运行 demo 相同的解释器构建；当前环境默认使用 `/usr/bin/python3`。如果使用 Conda Python，必须先安装对应的 Python 开发头文件和 pybind11。

```bash
RPHAND_PYTHON="$(command -v python3)" bash build.sh python
RPHAND_PYTHON="$(command -v python3)" source setup_python.sh
```

## Python 示例

先配置 CAN 接口并完成 Python 构建。确认设备和机械范围后运行：

```bash
RPHAND_PYTHON=/usr/bin/python3 source setup_python.sh
/usr/bin/python3 examples/wave_demo.py --side left
```

示例默认左手使用 `can0`、右手使用 `can3`，节点 ID 为 `1`。实机 wave 会执行使能、回零和连续位置控制，按 `Ctrl+C` 退出。

## Wave Demo

C++ 和 Python demo 使用相同的动作序列：拇指侧摆、拇指屈伸，然后四指 wave，持续运行直到按下 `Ctrl+C`。两者都会连接设备、使能并回零，请先确认机械范围和 CAN 接口。

```bash
# C++ demo（先执行 bash build.sh cpp）
./install/cpp/bin/rphand_wave_demo --side left

# Python demo（先执行 bash build.sh python，并使用同一个 Python 解释器）
RPHAND_PYTHON=/usr/bin/python3 source setup_python.sh
/usr/bin/python3 examples/wave_demo.py --side left
```

使用 `--side right` 运行右手；默认左手接口为 `can0`，右手接口为 `can3`，两者的设备节点 ID 都为 `1`。四指动作幅度可通过 `--amplitude 1..10000` 设置。

## C++ 接入

安装 C++ 组件后，外部 CMake 项目可使用：

```cmake
find_package(rphand CONFIG REQUIRED)
add_executable(hand_app main.cpp)
target_link_libraries(hand_app PRIVATE rphand::rphand)
```

配置项目时，将 `CMAKE_PREFIX_PATH` 指向 SDK 的 `install/cpp`。最小连接示例：

```cpp
#include <rphand/hand.hpp>

int main() {
    rphand::Hand hand;
    if (!hand.connect("can0", 1)) return 1;
    auto state = hand.get_state();
    hand.disconnect();
    return state.feedback_valid ? 0 : 1;
}
```

## ROS 2

```bash
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 launch rphand_ros dual_hand.launch.py
```

默认启动左手 `can0` 和右手 `can3`，节点 ID 均为 `1`。启动后通过 reset 服务初始化设备：

```bash
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
ros2 topic echo /dexhand_left/state
ros2 run rphand_ros command.py --side left 0 0 1000 1000 1000 1000
```

ROS 2 节点、话题、参数、会话重建和退出清理说明见 [`docs/ros2.md`](docs/ros2.md)。

## 无硬件验证

```bash
cmake -S . -B build/check \
  -DRPHAND_BUILD_SMOKE=ON -DRPHAND_BUILD_DEMO=ON
cmake --build build/check --parallel 2
ctest --test-dir build/check --output-on-failure
```

Python 示例测试：

```bash
source setup_python.sh
python3 -m unittest discover -s tests -p 'test_python_sdk.py' -v
```

这些测试不会连接真实 CAN 设备。

## 文档

- [`docs/api.md`](docs/api.md)：C++ / Python API、单位、状态和动作流程
- [`docs/architecture.md`](docs/architecture.md)：线程、通信协议和构建产物
- [`docs/ros2.md`](docs/ros2.md)：ROS 2 节点、话题、服务、参数和示例

## 许可

当前仓库未附带单独许可证文件。如需在组织或公开项目中分发，请先确认项目许可证和设备协议的授权范围。
