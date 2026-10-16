# ROS

包名 `rphand_ros`，可执行程序和节点名为 `rphand_node`。每个节点管理一只手，通过 C++ `rphand::Hand` 完成设备操作。

源码依据：[节点](../ros2/rphand_ros/src/rphand_node.cpp)、[消息](../ros2/rphand_ros/msg/DexHandState.msg)、[launch](../ros2/rphand_ros/launch/dual_hand.launch.py)、[默认配置](../ros2/rphand_ros/config/defaults.yaml)。另见 [架构](architecture.md) 和 [C++ / Python API](api.md)。

## 构建与启动

以下命令在 SDK 根目录执行。构建脚本要求 `/opt/ros/jazzy/setup.bash` 和 colcon；包依赖 ament、rclcpp、rclpy、rosidl、std_msgs、std_srvs、sensor_msgs 及 launch 组件，完整声明见 [package.xml](../ros2/rphand_ros/package.xml)。

```bash
bash build.sh ros
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 launch rphand_ros dual_hand.launch.py
```

默认启动左右两个节点：

| 手 | 节点 | 接口 | 设备节点 ID |
| --- | --- | --- | --- |
| 左 | `/dexhand_left/rphand_node` | can0 | 1 |
| 右 | `/dexhand_right/rphand_node` | can3 | 1 |

启动时不连接设备，也不使能或回零，仅发布无有效反馈的状态并等待 reset。CAN 接口需要由系统预先配置为 UP 和 CAN FD 模式。

```bash
# 只启动左手，指定接口和目标发送频率
ros2 launch rphand_ros dual_hand.launch.py \
  right:=false left_interface:=can0 left_node_id:=1 command_rate_hz:=50

# 直接运行单节点，不加载 defaults.yaml
ros2 run rphand_ros rphand_node --ros-args \
  -r __ns:=/dexhand_left -p interface:=can0 -p node_id:=1
```

## 话题与服务

| 相对名称 | 类型 | 行为 |
| --- | --- | --- |
| `cmd` | `std_msgs/msg/Int32MultiArray` | 订阅六轴绝对位置目标，各值 0..10000 |
| `state` | `rphand_ros/msg/DexHandState` | 发布反馈快照和有效性 |
| `reset` | `std_srvs/srv/Trigger` | 受理会话重建、参数配置、使能和回零 |

左右前缀分别为 `/dexhand_left`、`/dexhand_right`。cmd/state 均使用 Reliable、Volatile、KeepLast(1)。没有 stop 服务，也没有 phase 字段或流程阶段枚举。

cmd 数组顺序为拇指侧摆、拇指屈伸、食指、中指、无名指、小指。长度不是 6 或任意值越界时拒绝。初始化完成前、reset 期间和退出期间忽略目标。

state 包含 `header`、`feedback_valid`，以及六元素数组 `positions`、`velocities`、`currents`、`enabled`、`homed`、`torque_reached`、`fault_codes`。字段含义见 [API](api.md)。`header.stamp` 是 ROS 发布时间，不是硬件采样时间。

## 初始化和会话重建

```bash
# 会使能并驱动整手回零
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
ros2 topic echo /dexhand_left/state
```

服务立即返回受理结果：`success=true` 只代表请求已接受；已有 reset 在执行或节点正在退出时，返回 `success=false`、`message=Busy`。

实际流程由管理线程和会话线程执行：

1. 禁止接收新目标并清空缓存，通知旧会话退出。
2. 等待旧会话的 SDK 调用返回，完成清理并结束线程。
3. 创建新的 Hand，调用 connect，再调用 disable。
4. 依次设置速度、电流限制、力矩到位模式。
5. 调用 enable 和 home。
6. 成功后允许接收目标，开始周期发送与状态发布。

完成日志为 `Reset complete; waiting for a new target`。失败日志为 `Reset failed at ...`，随后清理会话，可再次 reset。没有异步结果话题或操作编号；应等待本次完成日志，并结合最新反馈确认六轴已使能、回零且无故障，不能把旧反馈当作本次成功。

## 周期发送与状态发布

收到首条有效目标后，节点按 `command_rate_hz` 持续发送最新缓存；没有新 cmd 时仍重复最后目标。没有插值、命令队列、目标时间戳或命令超时自动停止。reset 和退出会清空目标。

状态发布目标周期为 20 ms，即 50 Hz。目标发送与状态发布在同一会话线程执行，因此初始化、回零和清理期间暂停状态发布，阻塞的 SDK 调用也会影响周期。错过的周期会跳过，不补发积压目标。

位置发送失败会记录日志，后续周期继续尝试；节点没有根据反馈失效、故障或回零标志丢失自动停止的流程。调用方应自行监测设备状态和消息新鲜度。

## 参数与 launch

节点参数声明为只读，启动后不能动态修改。

| 参数 | 直接运行节点 | defaults.yaml | 约束 |
| --- | --- | --- | --- |
| `interface` | can0 | 未设置 | 非空 |
| `node_id` | 1 | 未设置 | 1..127 |
| `speed` | 8000 | 16000 | 1..20000 行程当量/秒 |
| `current_limit` | 400 | 400 | 0..3000，额定电流千分比 |
| `torque_reached_stop` | true | false | 力矩到位时是否停止 |
| `command_rate_hz` | 200 | 50 | 50、100、150、200 |

双手 launch 加载 YAML 后，再用 launch 参数覆盖接口、节点 ID 和发送频率。因此默认 launch 实际使用速度 16000、电流 400、力矩到位不停、发送频率 200 Hz；YAML 中的 50 Hz 会被默认 launch 参数覆盖。

| launch 参数 | 默认值 |
| --- | --- |
| `config` | 包内 config/defaults.yaml |
| `left` / `right` | true / true |
| `left_interface` / `right_interface` | can0 / can3 |
| `left_node_id` / `right_node_id` | 1 / 1 |
| `command_rate_hz` | 200 |
| `joy_mapper` | false |

```bash
ros2 launch rphand_ros dual_hand.launch.py \
  config:=/absolute/path/hand.yaml command_rate_hz:=50
```

## 退出清理

正常退出、重建旧会话或初始化失败时，若 Hand 仍连接，节点调用 C++ `prepare_disconnect()`：停止和失能各最多尝试一次；停止失败仍尝试失能，已有有效反馈满足条件时可跳过发送。之后无论清理返回值如何，都会 disconnect 并释放对象。

退出意图在当前同步 SDK 调用返回后检查，不会立即打断连接重试或回零。因此单次清理不等于整个退出过程有固定时限。强制终止或通信中断也不能保证设备已停止。

停止目标发布不会清空驱动缓存；正常退出驱动节点才会触发上述清理。reset 会继续使能和回零，不是停止命令。

## 示例脚本

在本次初始化完成后运行：

```bash
ros2 run rphand_ros command.py --side left 0 0 1000 1000 1000 1000
ros2 run rphand_ros wave.py --side left --amplitude 6000
```

两者只等待 cmd 订阅端出现，不检查设备状态。`command.py` 发布一次六轴目标；`wave.py` 以 50 Hz 执行一轮连续动作：拇指侧摆 `0 → 10000 → 0` 用时 2 秒，拇指屈伸相同动作再用 2 秒，随后四指 wave 6 秒、周期 1 秒，默认幅度 6000。总时长 10 秒，不等待到位或归零，正常结束发送全零。`--amplitude` 只影响四指，拇指始终使用全行程目标。全零是位置目标，不等于停止或回零。Ctrl+C 退出发布不保证停止设备。

手柄映射可通过 `joy_mapper:=true` 随 launch 启动，或单独运行 `ros2 run rphand_ros rphand_joy_mapper.py`。脚本订阅 `/joy`，默认横轴 6、纵轴 7、阈值 0.5：上/下请求左/右手 reset，左/右启动左/右手 wave。

按钮 2 的上升沿切换脚本内部“电机使能”标志：切到开启时请求双手 reset，切到关闭时不操作手。它不读取整机电机状态或手部反馈。reset 请求和 wave 发布独立，没有手柄断线自动停止。

手柄每次 wave 触发也执行一轮上述动作，与命令行共用 `scripts/wave_motion.py`。参数为 `horizontal_axis=6`、`vertical_axis=7`、`motor_enable_button=2`、`threshold=0.5`、`wave_amplitude=6000`、`wave_duration=6.0`。幅度只影响四指，要求 1..10000；时长只控制四指阶段，要求正数且有限，总时长为该值加 4 秒拇指动作。避免多个脚本同时向同一 cmd 话题发目标。

## 无硬件验证

加载 ROS 和包安装环境后执行：

```bash
ROS_DOMAIN_ID=187 ROS_LOCALHOST_ONLY=1 /usr/bin/python3 tests/ros_lifecycle.py \
  "$(ros2 pkg prefix rphand_ros)/lib/rphand_ros/rphand_node"
```

测试自行启动节点，使用超过 Linux 接口名长度上限的名称，检查空闲状态、reset 受理及 Busy、连接失败后的恢复和退出，不连接真实 CAN。它不验证实机回零或运动效果。
