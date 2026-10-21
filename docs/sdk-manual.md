# RP Hand SDK 功能手册

> 文档版本：0.1；适用 SDK 版本：0.1.0；维护依据：当前仓库公共接口。

本文档说明当前仓库提供的 RP Hand SDK。SDK 面向六轴灵巧手，提供 C++、Python 和 ROS 2 接口；C++ 核心负责连接、CAN FD 通信、状态缓存和基础运动控制。

本文档只描述当前仓库已经实现并公开的接口。协议细节、内部线程和重试实现见 [C++ / Python API](api.md)，安装和硬件准备见 [使用指南](getting-started.md)。

## 1. 支持范围

| 项目 | 当前实现 |
| --- | --- |
| 控制轴数 | 6 个主动轴 |
| 通信 | CAN FD + BRS；仲裁段 1 Mbps，数据段 5 Mbps |
| Linux 后端 | SocketCAN |
| Windows 后端 | gs_usb / WinUSB，当前支持 A8FA:8598 单通道适配器 |
| 语言 | C++17、Python 3.9+ |
| 框架 | ROS 2 节点 `rphand_ros` |
| 默认节点 ID | `1` |

SDK 不负责配置 Linux CAN 接口，也不负责给设备供电。Linux 应用启动前必须先把接口配置为 CAN FD、UP 状态和产品要求的两段速率。

## 2. SDK 包结构

```text
include/rphand/       C++ 公共头文件
src/                  C++ 核心实现
python/rphand/        Python 包和类型声明
examples/cpp/         C++ 示例
examples/python/      Python 示例
ros2/rphand_ros/      ROS 2 包
docs/                 SDK 文档
```

C++ 公共入口是 `<rphand/hand.hpp>` 中的 `rphand::Hand`。Python 公共入口是 `rphand.Hand`。

## 3. 六轴编号与单位

| 数组下标 | 单轴 ID | 轴名称 |
| --- | --- | --- |
| `0` | `1` | 拇指侧摆 |
| `1` | `2` | 拇指屈伸 |
| `2` | `3` | 食指 |
| `3` | `4` | 中指 |
| `4` | `5` | 无名指 |
| `5` | `6` | 小指 |

- 位置是绝对行程当量，范围为 `0..10000`。
- 速度使用行程当量/秒；协议编码按 10 行程当量/秒的步长处理，非整十数会截断。
- 电流限制是额定电流千分比，范围为 `0..3000`，不是 mA。
- 反馈电流是协议解码值，不能直接当作力或力矩。
- 单轴接口的 ID 只能是 `1..6`；需要控制整手时使用整手接口。

## 4. 核心类型

### `Backend`

```cpp
enum class Backend {
    SocketCan,
    GsUsbWinUsb,
};
```

Python 中对应 `Backend.SocketCan` 和 `Backend.GsUsbWinUsb`。

### `ConnectionOptions`

```cpp
struct ConnectionOptions {
    Backend backend = Backend::SocketCan;
    std::string interface = "can0";
    std::string serial;
    int channel = 0;
    int node_id = 1;
    int bitrate = 1000000;
    int data_bitrate = 5000000;
};
```

Linux 使用 `SocketCan` 和 `interface`。Linux 的速率由操作系统预先配置，SDK 不修改系统接口。

Windows 使用 `GsUsbWinUsb` 和实际适配器 `serial`。当前只支持单通道，`channel` 使用默认值 `0`。速率字段允许高级覆盖，普通产品配置不需要修改。

### `HandState`

`get_state()` 返回最新缓存快照，不会主动向设备发送查询。状态包含以下字段：

| 字段 | C++ 类型 | Python 类型 | 含义 |
| --- | --- | --- | --- |
| `positions` | `std::array<int, 6>` | 六元素整数元组 | 六轴当前位置 |
| `velocities` | `std::array<int, 6>` | 六元素整数元组 | 六轴速度 |
| `currents` | `std::array<int, 6>` | 六元素整数元组 | 协议电流值 |
| `enabled` | `std::array<bool, 6>` | 六元素布尔元组 | 六轴使能状态 |
| `homed` | `std::array<bool, 6>` | 六元素布尔元组 | 六轴回零标志 |
| `torque_reached` | `std::array<bool, 6>` | 六元素布尔元组 | 力矩到位标志 |
| `fault_codes` | `std::array<int, 6>` | 六元素整数元组 | 故障码低四位，`0` 表示无故障 |
| `feedback_valid` | `bool` | `bool` | 两组反馈均已收到且未过期 |

只有在 `feedback_valid` 为 `true` 时才应使用反馈数组做运动判断。反馈失效后，数组字段可能仍保留旧值。

## 5. 标准控制流程

建议所有产生实际动作的应用遵循以下顺序：

```text
创建 Hand
  -> connect
  -> disable
  -> set_speed / set_current_limit / set_torque_reached_mode
  -> enable
  -> home
  -> 周期调用 set_positions
  -> stop
  -> disable
  -> disconnect
```

每一步返回 `false` 后都应停止推进后续动作，并记录 stderr 中的错误信息。 `home()` 会实际驱动设备运动；`set_positions()` 只发送目标，不等待到位。

`connect()` 不是只读操作：连接时会配置反馈，并可能为了恢复遗留软件急停而失能设备。 `disconnect()` 不自动停止或失能，因此退出前应显式执行停止和失能流程。

## 6. C++ 接口

头文件：

```cpp
#include <rphand/hand.hpp>
```

### 创建和连接

```cpp
rphand::Hand hand;
rphand::ConnectionOptions options;
options.backend = rphand::Backend::SocketCan;
options.interface = "can0";
options.node_id = 1;

if (!hand.connect(options)) {
    return 1;
}
```

也可以使用兼容入口：

```cpp
bool ok = hand.connect("can0", 1);
```

该重载始终使用 SocketCAN。重复连接前必须先调用 `disconnect()`。

### 连接状态

```cpp
bool connected = hand.is_connected();
hand.disconnect();
```

`is_connected()` 只表示本地会话状态，不代表设备反馈仍然有效。

### 使能、失能和停止

```cpp
hand.disable();
hand.set_speed(16000);
hand.set_current_limit(400);
hand.set_torque_reached_mode(false);
hand.enable();
hand.home();

// 停止运动但不等价于失能
hand.stop();
hand.disable();
```

使能、失能和停止会等待相应状态反馈确认。 `stop()` 后如果要重新开始运动，应用应重新执行失能、连接、配置、使能和回零流程。

如果应用使用 C++，也可以在仍保持连接时调用一次退出准备：

```cpp
if (!hand.prepare_disconnect()) {
    // 停止或失能未确认；仍应调用 disconnect() 释放本地资源
}
hand.disconnect();
```

`prepare_disconnect()` 最多各尝试一次停止和失能，不关闭连接；停止失败时仍会继续尝试失能。Python 当前没有绑定此接口，应分别调用 `stop()` 和 `disable()`，再调用 `disconnect()`。

### 设置位置

```cpp
std::array<int, 6> positions{0, 0, 6000, 6000, 6000, 6000};
if (!hand.set_positions(positions)) {
    // 发送失败或当前会话拒绝调用
}

// 单轴调用仍然发送六轴目标，其他轴保持已缓存目标
hand.set_position(3, 7000);
```

位置接口只代表本次发送流程成功，不代表设备已经到位。SDK 不插值、不规划轨迹，也不自动周期重发目标。

### 读取状态

```cpp
const rphand::HandState state = hand.get_state();
if (!state.feedback_valid || state.fault_codes[0] != 0) {
    // 不应继续发送运动目标
}
```

## 7. Python 接口

安装后：

```python
from rphand import Backend, ConnectionOptions, Hand

options = ConnectionOptions()
options.backend = Backend.SocketCan
options.interface = "can0"
options.node_id = 1

hand = Hand()
try:
    if not hand.connect(options):
        raise RuntimeError("connect failed")
    if not hand.disable():
        raise RuntimeError("disable failed")
    if not hand.set_speed(16000):
        raise RuntimeError("set speed failed")
    if not hand.set_current_limit(400):
        raise RuntimeError("set current limit failed")
    if not hand.enable() or not hand.home():
        raise RuntimeError("initialization failed")

    if not hand.set_positions([0, 0, 6000, 6000, 6000, 6000]):
        raise RuntimeError("position send failed")
    print(hand.get_state().positions)
finally:
    if hand.is_connected():
        hand.disable()
    hand.disconnect()
```

Python 的 `set_positions()` 只接受恰好六个整数的 `list` 或 `tuple`。类型错误抛出 `TypeError`，长度错误或超出 C++ `int` 范围抛出 `ValueError`；进入 C++ 后的设备参数错误通常通过返回 `False` 表示。

完整示例：

- [Python 状态读取](../examples/python/read_state.py)
- [Python wave 动作](../examples/python/wave_demo.py)
- [C++ 状态读取](../examples/cpp/read_state.cpp)
- [C++ wave 动作](../examples/cpp/wave_demo.cpp)

## 8. 失败和退出语义

除 `disconnect()` 和 `get_state()` 外，公共控制方法通常返回 `bool`。`false` 可能表示参数不合法、未连接、当前调用忙、通信失败、取消或设备状态未确认。具体错误原因写入 stderr。

以下语义需要特别区分：

- 发送成功不等于设备执行完成；
- 控制命令返回成功不等于动作已经到位；
- 取消、超时或失败不等于设备已经物理停止；
- `disconnect()` 不保证设备停止或失能；
- `feedback_valid == false` 时不能继续使用旧反馈进行运动决策。

## 9. ROS 2 接口

ROS 2 包名为 `rphand_ros`，节点名为 `rphand_node`。每个节点控制一只手。

| 相对名称 | 类型 | 用途 |
| --- | --- | --- |
| `cmd` | `std_msgs/msg/Int32MultiArray` | 六轴绝对位置目标，范围 `0..10000` |
| `state` | `rphand_ros/msg/DexHandState` | 发布反馈状态 |
| `reset` | `std_srvs/srv/Trigger` | 重建会话、配置、使能并回零 |

默认 launch 可启动左右两只手。启动节点本身不会自动连接、使能或回零；调用 `reset` 后才执行完整初始化流程。 `reset` 返回 `success=true` 只表示请求已接受，应用仍需结合日志和 `state` 确认初始化完成。

ROS 2 的构建、参数、launch 和话题细节见 [ROS 2 文档](ros2.md)。

## 10. 常见问题

| 现象 | 检查项 |
| --- | --- |
| Windows 无法打开适配器 | WinUSB 驱动、适配器型号、实际序列号、是否被其他程序占用 |
| Linux 接口不存在 | CAN 驱动、接口名称、接口是否 UP、CAN FD 和两段速率 |
| 能打开适配器但无法连接设备 | 供电、CANH/CANL、终端电阻、节点 ID |
| 反馈无效 | 是否已配置反馈、总线是否有返回帧、反馈是否持续更新 |
| 运动调用失败 | 是否已连接、使能、回零，是否有故障码，是否仍有其他调用占用会话 |
| 退出后设备仍保持状态 | 退出前显式调用 `stop()`、`disable()`，再调用 `disconnect()` |

提交问题时请附上操作系统、适配器型号、SDK 版本或提交号、完整命令和首次失败日志，并隐藏适配器序列号等敏感信息。

## 11. 版本与实现依据

本文档对应仓库当前实现。接口变更时应同步检查：

- `include/rphand/hand.hpp`
- `include/rphand/types.hpp`
- `python/rphand/__init__.pyi`
- `examples/cpp/`
- `examples/python/`
- `docs/api.md`

其中 `docs/api.md` 是更精确的行为参考，本文档面向首次接入和按功能查找接口的用户。

## 12. 完整接口说明

本节按功能列出公共 C++ 接口和对应的 Python 调用。除特别说明外，控制接口均为同步调用；调用期间同一个 `Hand` 不接受另一个普通控制任务。接口返回 `false` 时，调用方应停止后续运动并检查 stderr 日志。

### 12.1 构造与对象生命周期

#### `Hand()`

**C++ 声明**

```cpp
rphand::Hand::Hand();
```

**功能**：创建一个未连接的本地控制对象。

**前置条件**：无。构造函数不会打开 CAN 设备，不会连接灵巧手，也不会产生运动。

**限制**：`Hand` 不可复制、不可移动。一个对象只能管理当前连接会话。

**Python 调用**

```python
hand = Hand()
```

#### `~Hand()`

析构时释放本地对象资源。析构不应被当作设备安全停机流程；应用退出前应显式调用 `stop()`、`disable()` 或 `prepare_disconnect()`，然后调用 `disconnect()`。

### 12.2 连接管理

#### `connect(const ConnectionOptions& options)`

**C++ 声明**

```cpp
bool connect(const rphand::ConnectionOptions& options);
```

**Python 调用**

```python
hand.connect(options) -> bool
```

**参数**：

| 参数 | 类型 | 要求 |
|---|---|---|
| `options.backend` | `Backend` | Linux 使用 `SocketCan`；Windows 使用 `GsUsbWinUsb` |
| `options.interface` | `str` / `std::string` | Linux CAN 接口名，不能为空 |
| `options.serial` | `str` / `std::string` | Windows 适配器实际十六进制序列号 |
| `options.channel` | `int` | 当前 Windows 单通道实现使用 `0` |
| `options.node_id` | `int` | `1..127` |
| `options.bitrate` | `int` | 默认 `1000000` |
| `options.data_bitrate` | `int` | 默认 `5000000` |

**返回值**：

- `true`：后端打开、协议初始化、反馈配置和连接检查完成。
- `false`：参数、后端打开、通信初始化或反馈检查失败。

**行为和注意事项**：

- Linux 速率必须由系统提前配置，SDK 不执行 `ip link`。
- Windows 会使用选定的适配器和速率建立 CAN FD 会话。
- 连接阶段可能失能设备并撤销遗留软件急停，因此不是只读查询。
- 初始化阶段最多尝试 30 次，包含第一次调用；失败后本次会话会关闭。
- 重复连接前必须先调用 `disconnect()`。

#### `connect(const std::string& can_name, int node_id)`

**C++ 声明**

```cpp
bool connect(const std::string& can_name, int node_id);
```

**Python 调用**

```python
hand.connect(can_name, node_id) -> bool
```

**参数**：`can_name` 是非空 SocketCAN 接口名；`node_id` 范围为 `1..127`。该兼容重载始终选择 `Backend::SocketCan`，不用于 Windows USB 适配器。

**返回值和行为**：与 `connect(ConnectionOptions)` 相同。

#### `disconnect()`

**C++ 声明**

```cpp
void disconnect();
```

**Python 调用**

```python
hand.disconnect() -> None
```

**功能**：取消本地任务，等待控制线程退出，并释放平台传输资源。

**注意事项**：

- 可重复调用。
- 不自动停止、不自动失能、不保证设备物理停止。
- 正在执行的底层阻塞调用返回前，取消不一定立即完成。

#### `is_connected() const`

**C++ 声明**

```cpp
bool is_connected() const;
```

**Python 调用**：`hand.is_connected() -> bool`

返回本地连接状态。返回 `true` 不代表 CAN 总线仍然正常，也不代表 `feedback_valid` 为 `true`。

### 12.3 使能、停止与回零

#### `enable()`

**C++ 声明**：`bool enable();`

**Python 调用**：`hand.enable() -> bool`

请求六轴使能，并等待反馈确认六轴状态。设备反馈必须有效且无故障；需要实际切换时，六轴应静止，SDK 会先使用位置模式。

返回 `true` 表示已经确认六轴处于使能状态，或调用前已满足该条件。最多尝试 30 次。返回 `false` 不代表设备一定保持失能，应用应重新读取状态。

#### `disable()`

**C++ 声明**：`bool disable();`

**Python 调用**：`hand.disable() -> bool`

取消旧任务，请求六轴失能，并等待反馈确认。返回 `true` 表示六轴失能已确认。最多尝试 30 次。

#### `stop()`

**C++ 声明**：`bool stop();`

**Python 调用**：`hand.stop() -> bool`

取消旧任务，请求整手停止，并等待六轴达到停止条件。`stop()` 保留连接和使能状态，不等价于 `disable()`；停止后重新运动应重新完成失能、连接、配置、使能和回零流程。

#### `prepare_disconnect()`

**C++ 声明**：`bool prepare_disconnect();`

**Python 调用**：未绑定。

在保持连接的情况下执行一次最佳努力清理：停止最多尝试一次，随后失能最多尝试一次。停止失败仍会继续失能。两者均确认成功才返回 `true`；该方法不关闭连接，之后仍需调用 `disconnect()`。

#### `home()`

**C++ 声明**：`bool home();`

**Python 调用**：`hand.home() -> bool`

驱动整手六轴回零，并观察本次回零的启动、完成和静止状态；完成后重复发送位置模式配置。

**前置条件**：应先连接、确认反馈有效、清除故障并使能。调用前应用还应确认设备处于可运动状态。

**返回值**：`true` 表示六轴本次回零完成并恢复位置模式；`false` 表示启动、反馈、超时、取消或状态检查失败。

**等待行为**：回零观察最多约 60 秒；启动阶段最多补发约 30 次。回零成功不表示后续目标已经发送。

### 12.4 运动参数与目标

#### `set_positions(const std::array<int, 6>& positions)`

**C++ 声明**

```cpp
bool set_positions(const std::array<int, 6>& positions);
```

**Python 调用**：`hand.set_positions(values) -> bool`

**参数**：六元素绝对位置数组，单位为行程当量，推荐范围 `0..10000`。

**返回值**：`true` 表示一次六轴位置帧发送流程成功；不表示设备已经到位。

**行为**：单次下发，不重试、不等待到位、不插值、不规划轨迹。需要连续运动时，应用应自行按周期调用，并持续检查 `feedback_valid`、`enabled`、`homed` 和 `fault_codes`。

**Python 参数规则**：必须是恰好六个整数的 `list` 或 `tuple`；不接受 `bool`、浮点数、字符串或 `None`。类型错误抛 `TypeError`，长度或 C++ `int` 范围错误抛 `ValueError`。

#### `set_position(int id, int position)`

**C++ 声明**：`bool set_position(int id, int position);`

**Python 调用**：`hand.set_position(id, position) -> bool`

`id` 为 `1..6` 的单轴 ID；`position` 为该轴绝对位置目标。该方法更新指定轴的本地目标缓存，但实际仍发送包含六轴目标的位置帧，其他轴继续使用已缓存目标。返回值只代表发送流程，不代表到位。

#### `set_speed(int speed)`

**C++ 声明**：`bool set_speed(int speed);`

**Python 调用**：`hand.set_speed(speed) -> bool`

设置六轴位置控制速度，单位为行程当量/秒。公共接口要求正整数；应用和 ROS 示例通常使用 `1..20000`。协议按 10 行程当量/秒编码，非整十值会截断。SDK 没有设备端速度读回，返回 `true` 代表发送和本地缓存核对成功。

#### `set_current_limit(int current)`

**C++ 声明**：`bool set_current_limit(int current);`

**Python 调用**：`hand.set_current_limit(current) -> bool`

设置六轴最大电流，单位为额定电流千分比，范围 `0..3000`。该值不是 mA，也不直接等于力矩。返回 `true` 代表发送和本地缓存核对成功，不代表设备端逐轴读回成功。

#### `set_torque_reached_mode(bool stop_on_reached)`

**C++ 声明**：`bool set_torque_reached_mode(bool stop_on_reached);`

**Python 调用**：`hand.set_torque_reached_mode(stop_on_reached) -> bool`

`true` 请求力矩到位后停止，`false` 请求保持设定电流的行为。设置写入六轴寄存器，但当前实现只读回首轴确认；返回 `true` 不能证明其余五轴均已完成设备端读回确认。

### 12.5 状态反馈

#### `get_state() const`

**C++ 声明**：`HandState get_state() const;`

**Python 调用**：`hand.get_state() -> HandState`

立即复制并返回最新状态缓存，不发送查询帧，不等待新反馈。位置反馈、状态反馈两组数据都必须收到，且各自年龄小于 100 ms，`feedback_valid` 才为 `true`。两组反馈不是同一时刻采样。

应用应首先检查：

```cpp
#include <algorithm>

auto state = hand.get_state();
const bool healthy = state.feedback_valid
    && std::all_of(state.enabled.begin(), state.enabled.end(), [](bool v) { return v; })
    && std::all_of(state.homed.begin(), state.homed.end(), [](bool v) { return v; })
    && std::none_of(state.fault_codes.begin(), state.fault_codes.end(), [](int v) { return v != 0; });
```

SDK 当前不提供故障清除接口。故障码清零、重新使能或重新连接应由上层结合设备状态决定。

## 13. 返回值、错误和重试规则

### 返回值约定

| 返回结果 | 含义 |
|---|---|
| `true` | 本次操作成功，或所要求的设备状态已经被确认 |
| `false` | 参数、连接、忙状态、取消、通信或设备状态确认失败 |
| `void` | `disconnect()` 只负责释放本地资源，不报告设备停机结果 |

返回 `true` 也有边界：位置发送成功不等于到位；参数发送成功不等于设备端读回；取消或超时不等于设备已经物理停止。

### 固定重试策略

SDK 内部重试次数不是用户可配置项。连接初始化、反馈配置、使能、失能和参数设置通常最多 30 次；位置目标只发送一次；`prepare_disconnect()` 的停止和失能各最多一次。详细时间和等待条件见 [API](api.md#重试与等待)。

### 线程和取消

普通接口是同步调用且同一个对象忙时会拒绝新任务。停止、失能、退出准备和断开可以使旧任务失效，但底层调用返回前不能保证取消已完成。Python 阻塞调用会释放 GIL；主线程信号处理仍可能等当前调用返回后才执行。

## 14. 完整示例：安全结束一个控制会话

```cpp
rphand::Hand hand;
rphand::ConnectionOptions options;
options.interface = "can0";

if (!hand.connect(options)) return 1;
if (!hand.disable()) return 1;
if (!hand.set_speed(16000)) return 1;
if (!hand.set_current_limit(400)) return 1;
if (!hand.set_torque_reached_mode(false)) return 1;
if (!hand.enable()) return 1;
if (!hand.home()) return 1;

if (!hand.set_positions({0, 0, 6000, 6000, 6000, 6000})) {
    hand.prepare_disconnect();
    hand.disconnect();
    return 1;
}

const bool cleanup_ok = hand.prepare_disconnect();
hand.disconnect();
return cleanup_ok ? 0 : 1;
```

上例展示的是控制接口调用顺序，不包含轨迹规划、限位保护或应用层周期调度。生产应用仍应在周期发送目标时检查反馈有效性、使能、回零标志和故障码。

## 15. 安装与工程接入

### 15.1 Python 安装

在仓库根目录执行：

```bash
python -m pip install .
```

运行时必须使用安装 SDK 的同一个 Python 解释器：

```bash
python examples/python/read_state.py --interface can0
```

Windows 使用 USB 适配器序列号：

```powershell
python examples/python/read_state.py --serial YOUR_SERIAL
```

### 15.2 C++ 构建与安装

Linux：

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
cmake --install build/linux --prefix install/linux
```

Windows：

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel 2
cmake --install build/windows --config Release --prefix install/windows
```

外部 CMake 工程：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_app LANGUAGES CXX)

find_package(rphand CONFIG REQUIRED)
add_executable(hand_app main.cpp)
target_link_libraries(hand_app PRIVATE rphand::rphand)
```

配置外部工程时，将 SDK 安装前缀传给 CMake：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/absolute/path/to/install/linux
```

### 15.3 Linux CAN FD 准备

SDK 不创建或修改 SocketCAN 接口。以 `can0` 为例，应用启动前配置：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details -statistics link show can0
```

确认接口处于 `UP`，并且启用了 FD、仲裁段 1 Mbps 和数据段 5 Mbps。重新启动系统或重新插拔适配器后，可能需要再次配置。

### 15.4 Windows 适配器准备

当前实现使用兼容 gs_usb 的单通道 CAN FD 适配器。设备管理器中应安装 WinUSB 驱动，并从“设备实例路径”中取得实际序列号。该序列号需要传给 `ConnectionOptions.serial` 或示例的 `--serial` 参数。

同一个适配器不能同时被多个 SDK 进程占用。切换 C++、Python 或调试工具前，应先退出原控制程序。

## 16. ROS 2 详细接口

### 16.1 节点和命名空间

包名为 `rphand_ros`，可执行程序和节点名为 `rphand_node`。双手 launch 默认创建：

| 手 | 节点 | 默认接口 | 默认节点 ID |
|---|---|---|---|
| 左手 | `/dexhand_left/rphand_node` | `can0` | `1` |
| 右手 | `/dexhand_right/rphand_node` | `can3` | `1` |

节点启动后不会自动连接、使能或回零，而是等待 `reset` 服务。调用 `reset` 后，节点依次执行连接、失能、速度/电流/力矩模式配置、使能和回零。

### 16.2 话题

#### `cmd`

类型：`std_msgs/msg/Int32MultiArray`

发送六轴绝对位置目标，数组顺序为：拇指侧摆、拇指屈伸、食指、中指、无名指、小指。数组长度必须为 6，每个值必须在 `0..10000`。初始化完成前、`reset` 执行期间和节点退出期间收到的目标会被忽略。

节点收到首个有效目标后，以 `command_rate_hz` 周期发送最新目标。没有新目标时仍会重复最后一个目标；节点不提供插值、命令队列、目标时间戳或自动超时停止。

#### `state`

类型：`rphand_ros/msg/DexHandState`

发布 `header`、`feedback_valid` 以及六元素的 `positions`、`velocities`、`currents`、`enabled`、`homed`、`torque_reached` 和 `fault_codes`。`header.stamp` 是 ROS 发布时间，不是硬件采样时间。

调用方应同时检查 `feedback_valid`、六轴 `enabled`、六轴 `homed` 和 `fault_codes`，不能只依据位置字段。

### 16.3 `reset` 服务

类型：`std_srvs/srv/Trigger`

```bash
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
```

服务返回 `success=true` 只表示请求已被接受，不表示回零已经完成。应等待节点日志中的 `Reset complete; waiting for a new target`，并结合 `state` 话题确认六轴已经使能、回零且无故障。已有 reset 正在执行或节点正在退出时，服务可能返回 `success=false` 和 `Busy`。

### 16.4 ROS 2 参数

| 参数 | 默认值 | 约束和含义 |
|---|---:|---|
| `interface` | `can0` | 非空 SocketCAN 接口名 |
| `node_id` | `1` | `1..127` |
| `speed` | `8000` | `1..20000` 行程当量/秒 |
| `current_limit` | `400` | `0..3000`，额定电流千分比 |
| `torque_reached_stop` | `true` | 力矩到位后是否停止 |
| `command_rate_hz` | `200` | 支持 50、100、150、200 |

双手 launch 还提供 `left`、`right`、`left_interface`、`right_interface`、`left_node_id`、`right_node_id`、`config` 和 `joy_mapper` 参数。节点参数启动后不能动态修改。

## 17. 常见调用场景

### 17.1 只读取状态

状态读取也会建立通信和配置反馈，但不会主动使能或回零：

```python
from rphand import Backend, ConnectionOptions, Hand

options = ConnectionOptions()
options.backend = Backend.SocketCan
options.interface = "can0"

hand = Hand()
try:
    if hand.connect(options):
        state = hand.get_state()
        print("feedback_valid:", state.feedback_valid)
        print("positions:", state.positions)
        print("fault_codes:", state.fault_codes)
finally:
    hand.disconnect()
```

该场景不能保证保持设备原有抓握，因为连接阶段可能恢复遗留急停并失能设备。

### 17.2 周期位置控制

应用层负责周期调度和健康检查：

```python
import time

while True:
    state = hand.get_state()
    if not (state.feedback_valid and all(state.enabled)
            and all(state.homed) and not any(state.fault_codes)):
        raise RuntimeError("hand feedback is not healthy")
    if not hand.set_positions([0, 0, 6000, 6000, 6000, 6000]):
        raise RuntimeError("position send failed")
    time.sleep(0.02)
```

位置调用不补发丢失周期，也不替应用判断目标是否到位。发送周期、目标切换和退出策略由应用负责。

## 18. FAQ

### 为什么 `connect()` 成功但 `feedback_valid` 为 `false`？

`connect()` 返回成功表示连接初始化流程完成，但状态快照仍可能等待新的两组反馈。应用应等待反馈有效，并检查故障码和轴状态。

### 为什么 `set_positions()` 返回成功但手没有到位？

该返回值只表示位置帧发送流程成功。SDK 不等待到位、不做轨迹规划，也不保证设备执行目标。应用应周期发送目标并通过 `positions`、`velocities`、`enabled` 和 `fault_codes` 观察结果。

### 为什么 `disconnect()` 后手仍然保持使能？

`disconnect()` 只释放本地连接资源，不自动停止或失能。退出前应调用 `stop()`、`disable()`，或 C++ 中调用 `prepare_disconnect()` 后再 `disconnect()`。

### 为什么速度设置后实际速度不是传入值？

协议速度按 10 行程当量/秒的步长编码，非整十数会被截断。公共接口也不提供设备端速度读回。

### 如何清除故障？

当前 SDK 没有故障清除接口。需要根据设备故障码和产品手册处理物理原因，必要时重新连接或通过上位机完成恢复。

### 为什么 Python 参数报错类型和 C++ 返回值不同？

Python 绑定会在进入 C++ 前检查容器长度和基础类型，类型或长度错误抛出 `TypeError`/`ValueError`；设备通信、连接和控制状态错误则使用 `False` 返回。
