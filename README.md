# roboparty_dexhand

`roboparty_dexhand` 是面向 Linux 的 LHandPro 6DOF S 灵巧手 C++/Python
控制库，通过外部配置的 SocketCAN CAN-FD 接口进行通信。库提供统一的
`HandDriver` 工厂接口、运动控制和反馈读取能力。

## 项目用途

使用本库可以在机器人应用中创建 LHandPro 驱动、发送关节运动目标并读取
灵巧手反馈。日常使用顺序是先在系统中配置 CAN-FD，再按需完成一次反馈
周期配置，然后初始化驱动、控制关节，最后显式释放驱动资源。

## 支持范围

- Linux x86-64 与 AArch64；
- LHandPro 6DOF S，CAN-FD，CANopen node ID 为 `1..127`；
- Python 模块 `dexhand_py` 与 C++ 头文件 `include/hand_driver.hpp`；
- 一个进程使用一个活动的厂商 SDK 实例。

本部署只支持 LHandPro 6DOF S。公共模型值
`LHANDPRO_6DOF` 对应该型号；普通厂商 6DOF 型号和当前 16DOF 型号不在
本部署的支持范围内。

## 安装

在 Debian 系统上安装 ARM64 软件包：

```bash
sudo apt install ./roboparty-dexhand_<version>_arm64.deb
```

将 `<version>` 替换为实际软件包文件名中的版本号，例如软件包文件为
`roboparty-dexhand_0.3.0_arm64.deb` 时使用 `0.3.0`。安装后可直接使用
已安装的 `dexhand_py` Python 模块和 `roboparty-dexhand-config` 命令。

## Linux CAN-FD 设置

CAN-FD 接口由系统或部署脚本在库外配置。下面以 `can0` 为例，设置名义
速率 1 Mbps、数据速率 5 Mbps、采样点和同步跳转宽度后启动接口：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can \
  bitrate 1000000 sample-point 0.8 sjw 4 \
  dbitrate 5000000 dsample-point 0.75 dsjw 2 fd on
sudo ip link set can0 txqueuelen 10000
sudo ip link set can0 up
ip -details -statistics link show can0
```

将命令中的 `can0` 替换为实际连接灵巧手的物理接口。库只使用已存在的
SocketCAN 接口，永远不会替应用配置速率、采样点、队列长度或接口状态。

## 首次配置反馈频率

新手、替换手、恢复出厂设置的手，或当前配置未知的手，需要完成一次
反馈周期配置；普通每次启动不需要重复执行。操作前先停止其他手控制进程，
然后执行：

```bash
roboparty-dexhand-config feedback-period apply --interface can0 --node-id 1 --milliseconds 20 --save
```

只给灵巧手本体断电再上电，然后执行查询：

```bash
roboparty-dexhand-config feedback-period show --interface can0 --node-id 1
```

查询结果中六个轴的 raw value 都必须是 `200`。`20 ms` 表示两个反馈类型
各自以 `50 Hz` 发送，因此合计约为 `100` 帧/秒。该配置由 CLI 持久化，
正常的 `init_hand()` 永远不会写入或修改持久化反馈周期参数。

## Python 使用

下面示例使用已安装的模块。它会将六个关节移动到有限的目标位置，读取
全部位置，再回到零位并再次读取。真实硬件会产生运动；运行前确认手的
周围没有人员或障碍物，并准备好立即断电或停止运动。

```python
import time

from dexhand_py import HandDriver, HandModel


CAN_INTERFACE = "can0"
TARGET_POSITION = 1200
TARGET_VELOCITY = 2000


def read_positions(hand, label):
    positions = [hand.get_now_position(joint) for joint in range(1, 7)]
    values = " ".join(
        f"joint{joint}={position}"
        for joint, position in enumerate(positions, start=1)
    )
    print(f"{label}: {values}")


hand = HandDriver.create_hand(
    "LHandPro", "canfd", CAN_INTERFACE, HandModel.LHANDPRO_6DOF, 1
)
try:
    if not hand.init_hand(True, False, 0.0):
        raise RuntimeError("init_hand failed")

    hand.set_move_no_home(1)
    for joint in range(1, 7):
        hand.set_target_position(joint, TARGET_POSITION)
        hand.set_position_velocity(joint, TARGET_VELOCITY)
    hand.move_motors(0)
    time.sleep(1.0)
    read_positions(hand, "target")

    for joint in range(1, 7):
        hand.set_target_position(joint, 0)
        hand.set_position_velocity(joint, TARGET_VELOCITY)
    hand.move_motors(0)
    time.sleep(1.0)
    read_positions(hand, "zero")
finally:
    hand.deinit_hand()
```

## 选择 CAN 接口

`interface` 字符串决定 Linux 将数据发送到哪个 SocketCAN 接口。使用
`can0`、`can1`、`can2` 或 `can3` 时，只需把工厂参数或上面示例中的
`CAN_INTERFACE` 改成对应名称；库不会自动探测或选择端口。端口必须已经
连接到目标灵巧手并完成 CAN-FD 配置。

## 双手使用

支持的双手部署方式是每只手使用一个 OS 进程。厂商 SDK 每个进程只支持
一个活动实例，接收回调也是进程级资源；每个进程应传入自己负责的接口和
node ID。例如，应用脚本自行提供这两个参数时可以这样启动：

```bash
# 左手进程：物理接口 can0，node ID 1
python3 left_hand_control.py --interface can0 --node-id 1

# 右手进程：物理接口 can1，node ID 2
python3 right_hand_control.py --interface can1 --node-id 2
```

两个进程各自调用 `HandDriver.create_hand()` 并传入分配好的接口和 node ID。
`roboparty_deploy` 不会替应用自动选择 `can0`/`can1`，接口分配应由部署
配置和应用参数明确完成。

## C++ 使用

最小生命周期示例只使用公共头文件中的接口，并在正常路径和异常路径都
释放驱动：

```cpp
#include <hand_driver.hpp>

int main() {
    auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0", HAND_LHANDPRO_6DOF, 1);

    try {
        if (!hand->init_hand(true, false, 0.0)) {
            hand->deinit_hand();
            return 1;
        }

        hand->set_move_no_home(1);
        hand->set_target_position(1, 1200);
        hand->set_position_velocity(1, 2000);
        hand->move_motors(1);

        hand->deinit_hand();
        return 0;
    } catch (...) {
        hand->deinit_hand();
        throw;
    }
}
```

## 主要 API

以下是 `HandDriver` 的公共 Python/C++ API。C++ 方法使用 `hand->method()`，
Python 方法使用 `hand.method()`。

- 创建与生命周期：`create_hand()`、`init_hand(enable_motors, home_motors,
  home_wait_time)`、`deinit_hand()`。工厂参数依次为
  `hand_type`、`interface_type`、`interface`、`hand_model`、
  `canfd_node_id`；本部署使用 `"LHandPro"`、`"canfd"` 和
  C++ 模型常量 `HAND_LHANDPRO_6DOF`；Python 使用
  `HandModel.LHANDPRO_6DOF`。
- 枚举名称按语言区分：Python 使用 `HandCommType.CANFD` 和
  `HandModel.LHANDPRO_6DOF`；C++ 使用 `HandCommType::CANFD` 和
  `HAND_LHANDPRO_6DOF`。`HandModel.LHANDPRO_16DOF` 虽由 Python 绑定导出，
  但不受本部署支持。
- 运动执行：`move_motors(finger_id=0)`、`stop_motors(finger_id=0)`、
  `home_motors(finger_id=0)` 的默认 `finger_id` 是 `0`，表示广播到全部
  关节。`set_enable(finger_id, enable)` 必须显式传入 `finger_id`，但传入
  `0` 仍表示广播；`set_move_no_home(enable)` 没有 `finger_id`，只接受
  `1`（允许未完成回零时运动）或 `0`（要求先完成回零）。
- 目标参数：`set_target_position(finger_id, position)`（编码器计数）、
  `set_target_angle(finger_id, angle)`（角度）、
  `set_position_velocity(finger_id, velocity)`（计数/秒）、
  `set_max_current(finger_id, current)`（mA）。这些方法需要明确的
  `finger_id`，不使用 `0` 广播约定。
- 反馈与状态：`get_now_position(finger_id)`、`get_now_angle(finger_id)`、
  `get_now_status(finger_id)`、`get_now_current(finger_id)`、
  `get_now_alarm(finger_id)`、`clear_alarm(finger_id)`。`clear_alarm(0)`
  清除全部关节报警；其他读取方法按指定关节返回缓存值。
- 设备信息与健康：`get_dof()` 返回 `(total, active)`，
  `get_can_name()` 返回接口名，`check_health()` 在驱动故障时抛出异常。

## 必须遵守的运行注意事项

- 启动控制程序前，先在库外配置并拉起目标 SocketCAN CAN-FD 接口；node ID
  必须与实际灵巧手一致，并避免同一总线上出现冲突的 node ID。
- 首次或配置未知的手必须先完成 20 ms 反馈周期配置并确认六个 raw value
  都为 `200`；配置命令执行期间不要运行其他手控制进程。
- `init_hand()` 返回 `false` 或抛出异常时，不要继续发送运动命令。运动前
  根据实际机构确认目标位置、速度、负载和安全边界。
- 每个成功创建的驱动都必须显式调用 `deinit_hand()`；不要把 Python 对象
  销毁或 C++ 智能指针析构当作清理替代方案。
- 双手使用时保持一手一进程，并在每个进程中明确传入自己的 interface 和
  node ID。

## 从源码构建

需要 CMake 3.15 或更新版本，以及 Python 3、pybind11、spdlog 和 fmt。仓库
已包含对应架构的 LHandPro SDK 文件。配置、编译和安装：

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --parallel
cmake --install build --prefix "$PWD/install"
```

安装后，C++ 使用 `include/hand_driver.hpp`，Python 使用已安装的
`dexhand_py` 模块。应用仍须在库外准备好实际的 SocketCAN 接口。

## License

RoboParty 编写的源代码采用 GPL-3.0。仓库内捆绑的厂商 SDK 头文件和二进制
文件保留其自身的许可与再分发边界；相关说明请参阅
[`thirdparty/README.md`](thirdparty/README.md)。
