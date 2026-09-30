# C++ / Python API

C++ 以 [hand.hpp](../include/dh116s/hand.hpp) 为准，Python 类型接口见 [__init__.pyi](../python/roboparty_dexhand_sdk/__init__.pyi)。两者共用同一 C++ 实现，Python 不包含另一套设备驱动。

## 范围与单位

支持 DH116S 六个主动通道，单轴 ID 为 1..6；不向用户暴露广播 ID 0。

| 数组下标 | 单轴 ID | 通道 |
| --- | --- | --- |
| 0 | 1 | 拇指侧摆 |
| 1 | 2 | 拇指屈伸 |
| 2 | 3 | 食指屈伸 |
| 3 | 4 | 中指屈伸 |
| 4 | 5 | 无名指屈伸 |
| 5 | 6 | 小指屈伸 |

目标与反馈位置为厂家行程当量，目标范围 0..10000，表示绝对位置。目标速度使用厂家行程当量/秒；电流上限为厂家千分比（0..3000），不是 mA 或精确力矩。反馈速度、电流沿用厂家 SDK 解码单位，不自行换算为 rad/s、mA 或 Nm。

不提供故障清除、设备信息查询、触觉、力控、阻抗或轨迹规划接口。

## 接口速查

C++ 对象为 `dh116s::Hand`，Python 为 `from roboparty_dexhand_sdk import Hand`。

| 接口 | 参数与行为 | 成功含义 |
| --- | --- | --- |
| connect(can_name, node_id) | 接口非空，节点 1..127；阻塞初始化 | 型号、通信、反馈配置及必要恢复通过 |
| disconnect() | 取消本地操作并释放资源，返回 void/None | 不保证物理停止；重复调用可用 |
| is_connected() | 立即返回 bool | 本地连接状态，不代表反馈新鲜 |
| enable() | 使能六轴，不自动回零 | 有效反馈确认六轴使能 |
| disable() | 优先取消旧操作，失能六轴 | 有效反馈确认六轴失能 |
| stop() | 优先取消旧操作，尝试停止六轴 | 有效反馈满足六轴停止判定 |
| home() | 整手回零，不自动使能 | 本次六轴回零确认完成，切模式发送流程成功 |
| set_positions(positions) | 六个 0..10000 整数 | 厂家调用与发送检查通过，不确认到位 |
| set_position(id, position) | ID 1..6，位置 0..10000 | 同上；由厂家单轴 API 处理 |
| set_speed(speed) | 正整数；demo/ROS 限制 1..20000 | 配置调用及厂家缓存检查通过 |
| set_current_limit(current) | 整数 0..3000 | 配置调用及厂家缓存检查通过 |
| set_torque_reached_mode(bool) | true：到位停止；false：保持设定电流行为 | 配置调用及厂家缓存检查通过 |
| get_state() | 立即复制最新快照，不发查询 | 返回 HandState，需自行检查有效性 |

控制方法通常以 false 和 stderr 日志报告失败，不能将所有失败理解为硬件故障：参数非法、Busy、未连接、取消和发送失败也会返回 false。C++ 不声明 noexcept，资源分配等异常仍可能向调用者传播。

有效反馈已经满足使能、失能或停止条件时，相应动作可直接成功，不一定重新发送命令。状态确认与位置发送返回值的含义不同。

## 连接与恢复

正常使用顺序：

```text
connect
→ set_speed / set_current_limit / set_torque_reached_mode
→ enable
→ home
→ set_positions，并持续检查反馈
→ disable 或 stop（按用途选择）
→ disconnect
```

必须检查每一步返回值，失败后不要继续发送运动目标。准备好设备后，ROS 用户可直接使用 reset 执行完整初始化，见 [ROS 状态机](ros2.md)。

connect 会打开既有 SocketCAN 接口、初始化厂家库并检查六轴 DH116S 型号；配置六轴反馈周期为 20 ms，匹配 SDO 写响应并读回核对，实测反馈约 50 Hz。SDK 不创建接口、不配置 Linux 波特率，也不写 EEPROM。

初始化还设置厂家 `set_move_no_home(1)`。这是厂家调用成功检查，不是设备读回；应用仍应按 enable/home 流程使用。

发现遗留软件急停时，connect 会先确认失能、停止，再发送空闲控制值 0 和撤销急停值 8，并用反馈检查恢复结果。因此连接不等于纯只读，不保证保持原有抓握力。没有遗留急停时不会无条件失能现有设备，也不会自动使能或回零。

stop 后再次运动：

```text
disable → disconnect → connect
→ 恢复速度、电流及力矩到位配置
→ enable → home → 新目标
```

不要认为 enable 可以自动解除遗留急停。disconnect 关闭工作线程、接收线程、厂家对象和 CAN socket，不会关闭系统 CAN 网卡，也不代表设备停止。

## 回零与等待

home 首次及补发均调用厂家 `home_motors(0)`，驱动整只手。必须由调用方事先安排使能状态；当前实现在首次发回零命令前不进行完整反馈前置检查。

回零期间要求反馈有效、无故障、六轴使能，并观察每轴进入本次回零运行。尚有轴未观察到启动时，约每秒整手补发一次，总计最多 30 次；整手补发可能重新触发其他轴。全部观察到启动后只等待完成，回零观察期限为 60 秒。

确认回零完成后，调用 `set_control_mode(0, LCM_POSITION)` 30 次，间隔 20 ms，约增加 0.58 秒及调用耗时。正常情况下须全部发送检查成功；取消或反馈异常会提前结束。切模式期间继续检查反馈、使能及回零状态。该阶段没有设备端模式读回，不能据此保证位置控制已实际生效。

home 失败或被取消，不意味着物理停机。正常返回前没有四指运动自检。发送位置全零也不等同于 home。

内部等待上限不是硬实时承诺：

| 操作 | 当前策略 |
| --- | --- |
| 连接初始化、反馈配置 | 各阶段最多尝试 30 次 |
| 设置 move_no_home | 最多 30 次，失败间隔 100 ms |
| 急停恢复 | 失能与撤销阶段各最多 30 次，每次反馈等待最多 500 ms |
| enable / disable / stop | 最多 30 次，每次反馈等待最多 500 ms |
| 参数设置 | 最多 30 次，失败间隔 20 ms |
| 位置下发 | 单次调用，不由 SDK 周期重发 |

厂家调用、操作系统调度可能延长实际墙钟时间。ROS 的周期重发和 demo 的定时循环属于上层行为。

## HandState

```cpp
struct HandState {
    std::array<int, 6> positions;
    std::array<int, 6> velocities;
    std::array<int, 6> currents;
    std::array<bool, 6> enabled;
    std::array<bool, 6> homed;
    std::array<bool, 6> torque_reached;
    std::array<int, 6> fault_codes;
    bool feedback_valid;
};
```

positions/velocities/currents 来自位置反馈组；状态位和故障来自状态反馈组。它们不是同一时刻的原子硬件采样。

仅当连接有效、接收未失败、两组反馈均已到达且各自年龄小于 100 ms 时，feedback_valid 才为 true。旧数值可能保留，但失效后不可作为当前设备状态。有效不代表无故障、已使能、已回零或已到位。

fault_codes 取厂家状态2低四位：0 无故障，1 位置超差，2 超速，3 过流，4 过载，5 过压，6 欠压，7 编码器错误，8 堵转，15 其他。故障处理应遵循设备手册，SDK 不提供清除故障 API。

## 线程和生命周期

每个 Hand 包含独立控制线程、接收线程、厂家实例和 SocketCAN socket。普通控制操作串行执行，已有普通操作或优先操作占用时，新普通操作返回 Busy，不建立运动命令长队列。

get_state 可并发读取。另一线程可调用 stop、disable、disconnect 取消旧操作；厂家函数执行中不能被强行打断。多个优先请求也会取消较早请求，应用应安排请求顺序。

同一进程拒绝重复连接同一个 (接口名, 节点 ID)，不做跨进程设备锁。不要同时运行上位机、SDK demo 和 ROS 驱动控制同一只手。

Hand 不可复制或移动。析构仅释放资源，不保证停止或失能。销毁前结束所有引用该对象的用户线程，并显式执行所需停机操作。SIGKILL、断电、断网不能依赖软件清理。

## Python 差异

- 方法名、顺序和 bool 返回语义与 C++ 一致。
- set_positions 只接受六元素 list/tuple，元素必须为整数；不接受 bool 充当整数。
- 类型错误抛 TypeError，数组长度或整数超出 C++ int 范围抛 ValueError；可表示但超出设备范围的值通常由 C++ 返回 False。
- set_torque_reached_mode 必须传 bool。
- HandState 属性只读，数组为独立 tuple 快照，Hand 销毁后快照仍可读取。
- 底层阻塞调用释放 GIL；主线程直接执行阻塞调用时，Python 信号处理仍可能等调用返回。双手 demo 用应用工作线程执行阻塞阶段，以便主线程处理退出。

Python 不提供隐式周期控制、asyncio、反馈回调或 NumPy 强依赖。示例和基本调用见 [README](../README.md)。
