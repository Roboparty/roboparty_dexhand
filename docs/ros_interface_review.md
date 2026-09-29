# DexHand ROS 接口设计审阅稿

基于 ROS 2，在现有 SDK 上封装。本文为接口设计，尚未实现。

## 1. 接口总览

左右手共用节点代码和消息定义，各运行一个实例，独立控制。可由一个 launch 启动双手，也可只启动一只。

| 类型 | 左手 | 右手 | 用途 |
| --- | --- | --- | --- |
| Topic，订阅 | `/dexhand_left/cmd` | `/dexhand_right/cmd` | 六轴位置目标，兼容现有 50 Hz 发布 |
| Topic，发布 | `/dexhand_left/state` | `/dexhand_right/state` | 六轴反馈和执行阶段，50 Hz |
| Service | `/dexhand_left/stop` | `/dexhand_right/stop` | 接受停止请求，后台停止 |
| Service | `/dexhand_left/reset` | `/dexhand_right/reset` | 接受重置请求，后台完整初始化 |

节点使用 namespace `/dexhand_left` 或 `/dexhand_right` 和相对接口名，不使用额外包含节点名的 `~/cmd` 等私有名称。左右身份、CAN 接口、节点 ID 由启动配置指定。

## 2. Service：立即响应，后台执行

两个 Service 均使用 `std_srvs/srv/Trigger`，请求为空：

```text
---
bool success
string message
```

**`success=true` 仅表示请求已接受并安排执行，不表示设备操作完成。** 拒绝请求时返回 false，message 说明原因。执行阶段和最终状态通过 state 查看，不引入操作编号。

### stop

收到请求后，先禁止 cmd 和周期位置下发、清空目标缓存、标记取消当前 reset，并切换到 STOPPING，再立即返回已接受。后台调用 SDK `stop()`：

```text
STOPPING → STOPPED：反馈确认已停止
         → ERROR：停止失败或无法确认
```

- 可以打断 reset，包括回零过程；被取消的 reset 不得继续使能或进入 READY。
- 正在停止时重复调用，返回已接受，不重复排队；已确认停止且状态仍有效时也可直接成功。
- 设备失联时仍禁止本地控制并尝试停止，无法确认物理停止则进入 ERROR。
- 不默认追加失能，也不承诺停止后保持抓握。
- 停止后必须完整 reset，才能再次控制。

### reset

收到请求后，禁止 cmd 和周期位置下发、清空目标缓存、切换到初始化阶段，立即返回已接受。后台执行：

```text
结束旧操作、确认失能
→ 断开旧连接、重新连接
→ 确认失能，恢复速度、电流上限及力矩到位配置
→ 使能六轴
→ 整手 home，确认本次回零完成
→ 保持使能，直接进入 READY，等待新的 cmd
```

- reset 会驱动整只手运动，每次都重新连接并回零。
- 没有旧连接时直接尝试连接；断开连接本身不代表设备停止。
- 回零确认完成后，检查反馈有效、无故障、六轴保持使能及回零状态，满足条件即进入 READY。
- reset 期间丢弃 cmd，不缓存。
- 任一步失败都不能进入 READY，必要时尝试停止，执行异常通过 ERROR 和日志反映。
- reset 或 stop 尚在执行时，新的 reset 返回 false（Busy）；stop 始终可以优先打断 reset。

## 3. cmd：六轴位置

使用标准消息 `std_msgs/msg/Int32MultiArray`，不带时间戳：

```text
std_msgs/MultiArrayLayout layout
int32[] data
```

与 `/home/wsy/data/cwl/roboparty_ws` 的现有输出保持 Topic 名称和消息类型一致，上层无需修改发布接口；驱动仍需通过 reset 进入 READY 才会执行目标。

`layout` 留空，`data` 必须包含 6 个整数，顺序统一为：

```text
[拇指侧摆, 拇指屈伸, 食指屈伸, 中指屈伸, 无名指屈伸, 小指屈伸]
```

位置范围 `0..10000`，单位为厂家行程当量，不是弧度。数组长度不为 6 或任一轴越界时，整条拒绝。

**ROS 发布频率与底层下发频率独立：上层更新目标缓存，驱动按 200 Hz 持续调用 SDK `set_positions()` 下发最新六轴目标。**

| 环节 | 频率 | 行为 |
| --- | --- | --- |
| ROS cmd | 当前 50 Hz，后续可提高到 200 Hz | 收到有效消息后覆盖目标缓存 |
| SDK 位置下发 | 目标 200 Hz，每 5 ms 一次 | 读取缓存；没有新消息也重复下发最新目标 |
| ROS state | 50 Hz | 发布六轴反馈和执行阶段 |

上层 50 Hz 时，一组目标通常连续下发约 4 次，不插值、不生成中间位置：

```text
时间       0 ms    5 ms    10 ms   15 ms   20 ms
ROS 收到   A                             B
SDK 下发   A       A       A       A       B
```

- 只在 READY 接收，其他阶段直接丢弃；进入 READY 后收到第一条有效目标，才开始周期下发。
- 只保留最新目标，不排队追赶历史位置；没有新消息时仍按 200 Hz 重发缓存目标。
- stop/reset 禁止周期下发并清空缓存，恢复到 READY 后等待新的有效目标。
- 反馈失效或设备故障时终止周期位置下发，进入 ERROR 并尝试停止。
- 消息不带时间戳，无法识别网络中延迟到达的旧目标。
- QoS 使用 `KeepLast(1) + Volatile + Reliable`，与现有项目的发布端一致。

发布 cmd 不代表实际到位，实际位置看 state。当前不因上层停止发布而自动清空目标或停止重发；命令超时自动 stop 暂不加入。

200 Hz 是每只手调用六轴 SDK 接口的目标频率，不是 CAN 帧总频率。一次调用包含六轴操作，实际发送周期受 SDK 耗时、系统调度及 CAN 总线负载影响。重复下发不保证 CAN 仲裁优先级，也不保证拥塞时按时送达。

## 4. state：六轴反馈和执行阶段，50 Hz

自定义消息 `DexHandState`，保留反馈有效标志以区分新鲜反馈与旧缓存：

```text
std_msgs/Header header
uint8 phase
bool feedback_valid

int32[6] positions
int32[6] velocities
int32[6] currents
bool[6] enabled
bool[6] homed
bool[6] torque_reached
int32[6] fault_codes
```

数组顺序与 cmd 一致。positions 是实际位置；velocities、currents 沿用 SDK/厂家反馈单位，不能未经换算当成 rad/s、mA 或力矩。其余数组分别表示使能、回零、力矩到位和故障码。

`phase` 合并运行状态和执行阶段，建议枚举如下：

| 值 | 阶段 | 含义 |
| --- | --- | --- |
| 0 | UNINITIALIZED | 启动后等待 reset |
| 1 | CONNECTING | 处理旧连接、重新连接并确认失能 |
| 2 | CONFIGURING | 恢复运动配置 |
| 3 | ENABLING | 正在使能 |
| 4 | HOMING | 正在整手回零 |
| 5 | READY | 初始化完成，可以接收 cmd |
| 6 | STOPPING | 正在停止，尚未确认 |
| 7 | STOPPED | 已确认停止，等待 reset |
| 8 | ERROR | 执行失败或设备状态异常 |

state 持续按 50 Hz 发布，包括回零、重连和停止期间。阶段保持到下一次转换，不只发送一次；具体失败原因记录在日志中。

反馈过期时可保留最后数值，但必须设置 `feedback_valid=false`。header 表示消息发布时间，不代表硬件采样时间；位置与状态来自不同反馈帧。底层按 200 Hz 下发目标，不意味着设备提供 200 Hz 测量。

## 5. 上层使用流程

```text
启动节点，等待 reset
→ 调用 reset，收到已接受
→ 查看 state：连接、配置、使能、回零
→ phase 为 READY 后，接收现有项目按 50 Hz 发布的 cmd
→ 首条有效目标到达后，驱动按 200 Hz 持续下发最新目标
→ 需要停止时调用 stop，收到已接受
→ 查看 state：STOPPED 表示确认停止，ERROR 表示执行异常
→ 再次使用时重新 reset
```

本版适合每只手由一个上层程序统一管理。Service 已接受后，state 可能仍有传输中的旧消息，上层不能把旧 READY/STOPPED 当成本次结果，应观察请求后的阶段更新。
