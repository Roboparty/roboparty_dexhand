# C++ / Python API

C++ 入口是 `<rphand/hand.hpp>` 中的 `rphand::Hand`；Python 入口是 `rphand.Hand`。两者使用相同控制流程，Python 没有单独的重试线程或异步 API。

源码依据：[公共头文件](../include/rphand/hand.hpp)、[控制实现](../src/hand.cpp)、[Python 绑定](../python/bindings.cpp)、[类型声明](../python/rphand/__init__.pyi)。另见 [架构](architecture.md) 和 [ROS](ros2.md)。

## 构建与接入

安装和平台准备见 [使用指南](getting-started.md)。

外部 C++ 项目接入：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_app LANGUAGES CXX)
find_package(rphand CONFIG REQUIRED)
add_executable(hand_app main.cpp)
target_link_libraries(hand_app PRIVATE rphand::rphand)
```

配置外部项目时，通过 `-DCMAKE_PREFIX_PATH=/absolute/path/rphand/install/cpp` 指定安装位置。

## 通道与单位

| 数组下标 | 单轴 ID | 通道 |
| --- | --- | --- |
| 0 | 1 | 拇指侧摆 |
| 1 | 2 | 拇指屈伸 |
| 2 | 3 | 食指 |
| 3 | 4 | 中指 |
| 4 | 5 | 无名指 |
| 5 | 6 | 小指 |

位置目标为绝对行程当量，范围 `0..10000`；速度使用行程当量/秒。电流限制为额定电流千分比，范围 `0..3000`，不是 mA。反馈电流保留协议解码值，不能直接当作力或力矩。单轴公共接口只接受 ID `1..6`，不接受广播 ID 0。

## 方法

除 `disconnect()` 返回 void/None、`get_state()` 返回快照外，下表方法返回 bool。`false` 表示失败或未确认成功，具体原因写入 stderr，包括参数不合法、未连接、忙、取消、通信失败或设备状态异常。

| C++ 方法 | Python 调用 | 当前行为 |
| --- | --- | --- |
| `connect(const ConnectionOptions&)` | `connect(options)` | 使用明确后端与设备选择，默认速率 1 Mbps / 5 Mbps、节点 1；Linux 速率由系统设置 |
| `connect(const std::string&, int)` | `connect(can_name, node_id)` | 接口非空、节点 1..127；同步完成初始化及反馈检查。重复连接需先断开 |
| `disconnect()` | `disconnect()` | 取消任务、等待线程退出、释放平台传输资源；可重复调用，不自动停止或失能 |
| `is_connected() const` | `is_connected()` | 返回本地连接状态，不代表设备反馈正常 |
| `enable()` | `enable()` | 六轴使能；要求反馈有效且无故障。需要实际切换时，还要求六轴静止，先切位置模式 |
| `disable()` | `disable()` | 取消旧任务并请求六轴失能，等待状态确认 |
| `stop()` | `stop()` | 取消旧任务并请求整手停止，等待状态确认，不等价于失能 |
| `prepare_disconnect()` | 未绑定 | 停止和失能各最多尝试一次；停止失败仍尝试失能。两者均成功才返回 true，不关闭连接 |
| `home()` | `home()` | 整手回零，观察本次各轴启动、回零完成和静止，再重复发送位置模式 |
| `set_positions(const std::array<int, 6>&)` | `set_positions(values)` | 设置六轴目标并下发一次，不等待到位 |
| `set_position(int, int)` | `set_position(id, position)` | 更新指定轴缓存；其余轴保持缓存目标，实际仍发六轴位置帧 |
| `set_speed(int)` | `set_speed(speed)` | 设置六轴速度，公共接口检查正整数；应用建议使用 ROS/示例采用的 1..20000 范围 |
| `set_current_limit(int)` | `set_current_limit(current)` | 设置六轴电流限制，范围 0..3000 |
| `set_torque_reached_mode(bool)` | `set_torque_reached_mode(stop_on_reached)` | true 请求力矩到位停止，false 请求保持设定电流行为 |
| `get_state() const` | `get_state()` | 立即返回最新缓存快照，不发送设备查询 |

使能、失能或停止条件已经被有效反馈满足时，直接返回成功，不再发送对应命令。`prepare_disconnect()` 也遵循这一规则。

速度、电流设置通过发送结果及本地缓存核对确认，没有设备端读回。速度报文按 10 行程当量/秒的步长编码，非整十数会截断；公共接口没有额外速度上限校验。力矩到位模式写六轴寄存器，但只读回首轴，不能由成功返回值证明其余五轴均生效。

## 连接与运动流程

```text
connect → disable → 设置速度、电流、力矩到位模式
        → enable → home → 位置控制与反馈检查
        → 停止、失能 → disconnect
```

每一步失败后应停止推进后续运动。`connect()` 配置反馈，并可能为恢复遗留软件急停而失能和撤销急停；它不是只读操作，不保证保持原有抓握。

连接时底层允许无回零位置命令，因此“尚未回零”不一定阻止位置下发。应用应自行执行完整初始化，持续检查反馈、使能、回零和故障状态；位置方法不提供完整的健康状态门控。`home()` 首次发命令前也没有完整的运动状态检查，调用前应先使能并确认状态。

位置方法返回成功只代表本次发送流程成功，不代表到位。SDK 不插值、不规划轨迹、不周期重发目标；需要连续发送的应用自行调度，ROS 的周期行为见 [ROS](ros2.md)。

停止后恢复采用完整流程：`disable → disconnect → connect → 配置 → enable → home`。取消、超时和失败返回都不意味着设备已经物理停止。

## 重试与等待

所有“最多 30 次”均包含首次调用，成功、取消或错误条件可提前结束；这些次数不是用户可配置参数。

| 操作 | 策略 |
| --- | --- |
| 连接初始化 | 最多 30 次，失败后关闭本次会话，重试间隔 100 ms |
| 反馈配置 | 最多 30 次，阶段失败后间隔 500 ms；单次 SDO 等待最多 500 ms |
| 无回零命令选项 | 最多 30 次，失败间隔 100 ms，仅确认后端调用成功 |
| 遗留急停恢复 | 先失能确认、再撤销急停，各自最多 30 次 |
| 使能、失能、停止 | 最多 30 次；发送成功后等待新状态最多 500 ms，发送失败后等待 100 ms |
| 退出准备 | 停止、失能各最多 1 次；之后仍需显式 disconnect |
| 参数设置 | 最多 30 次，失败间隔 20 ms |
| 回零启动 | 未观察到所有轴启动时约每秒补发整手命令，总计最多 30 次 |
| 回零观察 | 首次回零命令返回后开始计时，观察最多 60 秒 |
| 回零后位置模式 | 正常连续发送 30 次，相邻间隔 20 ms；取消或状态异常可提前退出 |
| 位置目标 | 单次下发，不重试、不等待到位 |

全部轴已观察到本次回零启动后不再补发，只等待完成。模式重复发送不等于设备端模式读回。上述等待之外还有发送、底层调用和系统调度耗时，不是总耗时的硬上限。

## 状态快照

| 字段 | C++ 类型 | Python 类型 | 含义 |
| --- | --- | --- | --- |
| `positions` | `std::array<int, 6>` | 六元素 int tuple | 行程位置 |
| `velocities` | `std::array<int, 6>` | 六元素 int tuple | 行程速度 |
| `currents` | `std::array<int, 6>` | 六元素 int tuple | 协议电流值 |
| `enabled` | `std::array<bool, 6>` | 六元素 bool tuple | 使能状态 |
| `homed` | `std::array<bool, 6>` | 六元素 bool tuple | 回零标志 |
| `torque_reached` | `std::array<bool, 6>` | 六元素 bool tuple | 力矩到位标志 |
| `fault_codes` | `std::array<int, 6>` | 六元素 int tuple | 状态反馈中的故障码低四位，0 表示无故障 |
| `feedback_valid` | `bool` | `bool` | 连接正常且两组反馈均未过期 |

两组反馈都需收到且各自年龄小于 100 ms；位置组与状态组并非同时采样。失效后字段可保留旧值，不要只读数组而忽略 `feedback_valid`。SDK 不提供故障清除接口。

## Python 类型与线程

`set_positions` 只接受恰好六个整数的 list 或 tuple。整数参数不接受 bool、float、字符串或 None；错误类型抛 `TypeError`，数组长度错误或整数超出 C++ int 表示范围抛 `ValueError`。进入 C++ 后的位置、电流等范围错误通常返回 False。

`connect` 的接口名必须为 str，`set_torque_reached_mode` 必须传 bool。`HandState` 的 Python 属性只读，数组是独立 tuple。阻塞调用释放 GIL；主线程的 Python 信号处理仍可能等调用返回后才执行。

普通调用同步且忙时拒绝。另一线程可以通过停止、失能、退出准备或断开使旧任务失效；底层调用返回前不能保证取消完成。Hand 在 C++ 中不可复制或移动，析构只释放本地资源。

## 使用示例

最小状态读取：[C++](../examples/cpp/read_state.cpp) / [Python](../examples/python/read_state.py)。
完整动作：[C++ wave](../examples/cpp/wave_demo.cpp) / [Python wave](../examples/python/wave_demo.py)。
参数、动作范围和退出行为见 [使用指南](getting-started.md#示例参数与行为)。

实际运动结束时，C++ 可在仍连接时调用 `prepare_disconnect()`，检查结果后再断开。Python 没有这个接口，应分别调用 `stop()`、`disable()` 并检查各自结果，不能用短路表达式跳过第二步；这两个 Python 调用仍各自使用普通的最多 30 次策略。

## 连接配置

`Hand::connect(const ConnectionOptions&)` 与 Python `hand.connect(options)` 共用以下配置。
类型定义位于 `<rphand/types.hpp>`；包含 `<rphand/hand.hpp>` 时也会自动引入。

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `backend` | `Backend::SocketCan` | Windows 显式选择 `Backend::GsUsbWinUsb` |
| `interface` | `"can0"` | Linux 接口名，由系统预先配置为 UP / CAN FD |
| `serial` | 空字符串 | Windows 必填的实际适配器序列号 |
| `channel` | `0` | 当前 Windows 只支持单通道 |
| `node_id` | `1` | 范围 `1..127` |
| `bitrate` | `1000000` | 产品仲裁段速率，Windows 自动应用 |
| `data_bitrate` | `5000000` | 产品数据段速率，Windows 自动应用 |

Python 枚举写作 `Backend.SocketCan` / `Backend.GsUsbWinUsb`。
速率字段允许高级覆盖，正数校验和硬件位时序校验仍有效。Linux 后端不设置系统速率。
原 `connect(can_name, node_id)` 始终选择 SocketCAN，保持兼容。
连接选项必须明确选择正确平台后端，不会通过名称猜测 USB 或 CAN。
平台准备与示例见 [使用指南](getting-started.md)。
