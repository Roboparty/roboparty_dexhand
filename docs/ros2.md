# ROS 2 Jazzy

包名 roboparty_dexhand_ros，C++ 节点 dexhand_node。左右手使用同一节点实现、独立实例及 namespace。构建见 [构建指南](build.md)。

## 启动

每个终端在仓库根目录加载：

```bash
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
```

```bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py command_rate_hz:=200
# 只启动左手
ros2 launch roboparty_dexhand_ros dual_hand.launch.py right:=false
# 覆盖实际接线与节点
ros2 launch roboparty_dexhand_ros dual_hand.launch.py \
  left_interface:=can0 right_interface:=can3 left_node_id:=1 right_node_id:=1
```

启动后仅发布未初始化状态，不自动连接、使能或回零。双手默认 can0/can3、节点各 1，需事先配置好 CAN FD 接口。左右身份由 namespace 和启动参数指定，不自动识别。

## 参数

节点参数启动后只读：

| 参数 | 默认 | 范围 / 含义 |
| --- | --- | --- |
| interface | can0 | SocketCAN 接口；双手 launch 为右手覆盖成 can3 |
| node_id | 1 | 1..127 |
| speed | 8000 | 1..20000，厂家行程当量/秒 |
| current_limit | 400 | 0..3000，厂家千分比 |
| torque_reached_stop | true | 力矩到位后停止 |
| command_rate_hz | 200 | 50、100、150、200 |

修改 speed/current_limit/torque_reached_stop 可编辑 [defaults.yaml](../ros2/roboparty_dexhand_ros/config/defaults.yaml) 后重新构建，或传入自定义 YAML：

```yaml
/**:
  ros__parameters:
    speed: 8000
    current_limit: 400
    torque_reached_stop: true
```

```bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py \
  config:=/absolute/path/hand.yaml command_rate_hz:=50
```

launch 始终用自己的 command_rate_hz、接口及节点 ID 参数覆盖 YAML 对应值。直接 ros2 run 节点时才可仅由 YAML 设置这些参数。

## 接口

| 接口 | 类型 | 用途 |
| --- | --- | --- |
| /dexhand_left/cmd | std_msgs/msg/Int32MultiArray | 六轴目标输入 |
| /dexhand_left/state | roboparty_dexhand_ros/msg/DexHandState | 状态输出 |
| /dexhand_left/reset | std_srvs/srv/Trigger | 接受完整初始化请求 |
| /dexhand_left/stop | std_srvs/srv/Trigger | 接受停止请求 |

右手对应 dexhand_right。接口使用 namespace 下的相对名称，不带额外 dexhand_node 层级。

cmd.data 必须为六个 0..10000 整数，顺序为拇指侧摆、拇指屈伸、食指、中指、无名指、小指。layout 建议留空。长度或范围非法整条拒绝。

cmd/state QoS 为 Reliable、Volatile、KeepLast(1)。cmd 不带时间戳，驱动无法识别延迟抵达的旧目标。

仅 READY 接收 cmd；首条有效目标到达后按配置频率持续下发最新缓存，没有新消息也重发，不插值、不追赶历史目标。不存在命令超时自动 stop。50 Hz 上层发布与 200 Hz 底层重发是两个频率；一次六轴接口调用也不等于一帧 CAN。

频率为调度目标，150 Hz 使用纳秒周期，实际周期受 SDK 调用耗时、调度及总线负载影响。state 始终以目标 50 Hz 发布，包括回零期间。

## Reset 与 Stop

两个服务 success=true 都只表示请求已接受，不表示硬件动作完成。拒绝或 Busy 返回 false，具体原因在 message。

reset：

```text
禁止外部目标并清空缓存
→ 旧连接存在则确认失能
→ 断开旧连接并重新连接
→ 确认失能
→ 设置速度、电流、力矩到位行为
→ 使能
→ 整手回零
→ 切位置模式发送 30 次，间隔 20 ms
→ 检查反馈有效、无故障、六轴使能且已回零
→ READY，等待新目标
```

reset 会驱动整只手，不能在抓握负载时当作无运动操作。已取消四指到 1000 再回 0 的自检和相应的整轮自动重试；READY 不代表已经通过位置运动验证。任一步失败都不会进入 READY；驱动尝试停止并进入 ERROR，之后可再次手动 reset。

stop 先禁止周期位置下发、清空目标并取消 reset 后续步骤，后台调用 SDK stop。它可打断回零；连接操作期间等待连接阶段结束后再停止，不继续使能。STOPPED 表示已确认停止，ERROR 表示失败，不能将 ERROR 当作物理停止保证。stop 不附加失能，也不保证保持抓握。

reset/stop 执行中，新 reset 返回 Busy；重复 stop 不重复排队。停止后必须完整 reset。正常节点退出尝试 stop 后断开；强制终止、断电或断网不能保证物理停止。

## State

定义见 [DexHandState.msg](../ros2/roboparty_dexhand_ros/msg/DexHandState.msg)：

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

header 为消息发布时间，不是硬件采样时间。反馈过期可保留旧值，但 feedback_valid=false。数组单位及含义见 [API](api.md)。

| phase | 常量 | 含义 |
| --- | --- | --- |
| 0 | UNINITIALIZED | 等待 reset |
| 1 | CONNECTING | 旧连接处理、连接及失能确认 |
| 2 | CONFIGURING | 参数配置 |
| 3 | ENABLING | 使能 |
| 4 | HOMING | 回零及回零后的模式发送 |
| 5 | READY | 可接收新目标 |
| 6 | STOPPING | 正在停止 |
| 7 | STOPPED | 已确认停止，等待 reset |
| 8 | ERROR | 执行失败或状态异常 |
| 9 | VERIFYING | 为兼容保留，当前不发布 |

READY 期间反馈失效、故障、使能或回零状态丢失会终止目标下发，尝试 stop 后保持 ERROR。短暂阶段可能来不及发布；收到服务响应后也可能还有在途旧状态，不能把旧 READY 当成本次 reset 结果。接口没有操作编号。

## 左右手测试命令

先清空运动空间，确保只有一个程序管理对应手。各命令在加载环境后使用。

```bash
# 左右手 reset，分别运行
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
ros2 service call /dexhand_right/reset std_srvs/srv/Trigger '{}'

# 查看状态；对应手进入 READY 且 feedback_valid=true 后继续
ros2 topic echo /dexhand_left/state
ros2 topic echo /dexhand_right/state
```

查看状态命令持续运行，Ctrl+C 仅退出查看。波浪前等待对应手 READY：

```bash
ros2 run roboparty_dexhand_ros wave.py --side left
ros2 run roboparty_dexhand_ros wave.py --side right
```

ROS wave 默认幅度 6000、周期 1 秒、时长 6 秒、50 Hz 发布，首尾各 1 秒渐入渐出；可用 --amplitude 1000 调整。拇指目标保持 0。它不自行 reset；正常结束发送全零，Ctrl+C 只结束发布，驱动仍保留最后目标。实际跟踪受速度上限约束。

波浪退出后发送全零：

```bash
ros2 topic pub --once /dexhand_left/cmd std_msgs/msg/Int32MultiArray '{data: [0, 0, 0, 0, 0, 0]}'
ros2 topic pub --once /dexhand_right/cmd std_msgs/msg/Int32MultiArray '{data: [0, 0, 0, 0, 0, 0]}'
```

全零是位置目标，不是 stop，也不是整手 home。也可用检查新鲜 READY 后再发送的脚本：

```bash
ros2 run roboparty_dexhand_ros command.py --side left 0 0 1000 1000 1000 1000
```

```bash
ros2 service call /dexhand_left/stop std_srvs/srv/Trigger '{}'
ros2 service call /dexhand_right/stop std_srvs/srv/Trigger '{}'
```

## RP1 手柄适配示例

`dexhand_joy_mapper.py` 订阅 /joy，通过现有 reset 服务及 cmd topic 控制，不修改 C++ 核心，也不负责启动手柄驱动或 RP1 推理节点。

```bash
ros2 run roboparty_dexhand_ros dexhand_joy_mapper.py
```

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| horizontal_axis | 6 | 十字键横轴 |
| vertical_axis | 7 | 十字键纵轴 |
| motor_enable_button | 2 | RP1 电机切换按键索引 |
| threshold | 0.5 | 方向触发阈值 |
| wave_amplitude | 6000 | 四指幅度，1..10000 |
| wave_duration | 6.0 | 波浪时长，使用正的有限秒数 |

十字键正纵轴为上、正横轴为左：上/下对应左/右手 reset，左/右对应左/右手 wave。持续按住同一方向不会反复触发。不同手柄应先观察 /joy 的轴和按钮映射。

按钮 2 的上升沿切换适配脚本内部的布尔状态；它推算为“使能”的那次会 reset 双手，下一次不操作手。脚本不读取 RP1 实际电机使能结果，重启、丢事件或 RP1 操作失败可能导致不同步。

该示例保持 RP1 集成时的逻辑：reset 的 busy 在服务响应时即释放，并非等待回零结束；波浪只在开始时检查缓存 READY，没有反馈接收时间检查，也不在波浪期间持续检查状态。没有手柄 stop 映射，也没有手柄断联自动 stop。需要停止时调用服务；底层 ROS 驱动仍执行自身反馈保护。不要同时运行手柄波浪和其他 cmd 发布端。

RP1 的整机启动脚本属于部署项目，不随本 SDK 提供。
