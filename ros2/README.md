# DH116S ROS 2 Jazzy

## 下发频率与 reset

`command_rate_hz` 支持 50、100、150、200，默认 200，启动后只读。双手 launch 的同名参数覆盖 YAML 值；直接运行节点时可通过 YAML 或 `-p command_rate_hz:=50` 设置。状态发布保持 50 Hz。

```bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py command_rate_hz:=50
```

reset 不再执行四指运动自检，也不再因自检失败自动重复初始化。SDK 在整手回零完成后调用厂家 `set_control_mode(0, LCM_POSITION)` 30 次，调用间隔 20 ms；全部发送成功且反馈有效、无故障、六轴保持使能及回零状态后进入 READY，等待新的 cmd。

切模式期间 stop 可打断；反馈异常会提前结束，发送失败则初始化失败。重复发送不是设备模式读回确认。`VERIFYING=9` 常量仅为兼容保留，不再发布该阶段。SDK `home()` 的这一行为同时适用于 C++ 和 Python SDK（需重新编译各自使用的库）。

C++ 驱动复用仓库内 SDK；Python 负责 launch 和上层控制示例。消息及接口遵循 [接口设计](../docs/ros_interface_review.md)。

## 编译与部署

推荐在完整仓库根目录使用统一入口：

```bash
bash build.sh ros
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py command_rate_hz:=50
```

`bash build.sh all` 同时构建 C++、Python 和 ROS，分别安装到 `install/cpp`、`install/python`、`install/ros`。此方式与下方原有工作区方式二选一，不要在同一终端加载两份同名 ROS 包。

目标板 `robo@192.168.137.250`：ARM64、Ubuntu 24.04、ROS 2 Jazzy。新版工作区为 `~/dexhand_jazzy_ws`，保留板上原有 `~/roboparty_dexhand_ros`。不要同时启动旧驱动或其他操作同一只手的程序。

在本地仓库根目录同步完整源码（ROS 包直接编译上两级的 SDK）：

```bash
ssh robo@192.168.137.250 'mkdir -p ~/dexhand_jazzy_ws/src'
rsync -az --exclude=.git --exclude=build --exclude='build-*' --exclude=install \
  --exclude=__pycache__ ./ robo@192.168.137.250:~/dexhand_jazzy_ws/src/roboparty_dexhand_sdk/
```

板上新终端执行：

```bash
source /opt/ros/jazzy/setup.bash
cd ~/dexhand_jazzy_ws
colcon build --base-paths src/roboparty_dexhand_sdk/ros2 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
source install/local_setup.bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py
```

需要 `ament_cmake`、`rclcpp`、`rclpy`、`std_msgs`、`std_srvs`、`rosidl_default_generators`、`launch_ros` 和 colcon；目标板已提供。厂家 ARM64 动态库随安装携带，驱动通过相对 RPATH 加载。

默认左手 `can0`、右手 `can3`，各自 node ID 为 1，沿用板上旧 launch。CAN FD 接口需事先由系统配置为仲裁 1 Mbps、数据 5 Mbps；驱动不修改系统网络配置。

```bash
# 只启动左手
ros2 launch roboparty_dexhand_ros dual_hand.launch.py right:=false
# 覆盖连接配置
ros2 launch roboparty_dexhand_ros dual_hand.launch.py \
  left_interface:=can0 right_interface:=can3 left_node_id:=1 right_node_id:=1
```

`config/defaults.yaml` 设置 `speed`（1..20000，默认 8000）、`current_limit`（0..3000，默认 400）、`torque_reached_stop`（默认 true）。可通过 `config:=/absolute/path/config.yaml` 替换。参数启动后只读，更改需要重启节点；速度与电流单位沿用 SDK。

## 控制

单手四指波浪（先 reset 并等待 READY）：

```bash
ros2 run roboparty_dexhand_ros wave.py --side left
```

通过 cmd topic 以 50 Hz 发布，拇指目标为 0，四指幅度默认 6000，周期 1 秒，总长 6 秒，首尾各 1 秒渐入渐出，正常结束发送全零。可使用 `--amplitude 1000` 调整幅度，`--side right` 选择右手。不会自行 reset；Ctrl+C 只结束发布，驱动仍保留最后目标。实际运动速度受驱动的速度上限约束。

退出波浪后控制全零（不要同时运行两个目标发布端）：

```bash
ros2 topic pub --once /dexhand_left/cmd std_msgs/msg/Int32MultiArray '{data: [0, 0, 0, 0, 0, 0]}'
```

全零是位置目标，不是 stop 或重新回零。

启动不连接、不使能、不自动回零，持续发布 UNINITIALIZED。**reset 会使能并驱动整手回零**，执行前清空手部运动空间。

```bash
source /opt/ros/jazzy/setup.bash
source ~/dexhand_jazzy_ws/install/local_setup.bash
ros2 service call /dexhand_left/reset std_srvs/srv/Trigger '{}'
ros2 topic echo /dexhand_left/state
```

Service 的 success 仅表示接受请求。观察请求后的阶段变化，等到 `phase: 5`（READY）且 `feedback_valid: true` 后再发目标。右手将名称中的 `left` 替换成 `right`。

```bash
# 一次发送目标；示例只在收到新鲜 READY 状态时发送，不会自行 reset
ros2 run roboparty_dexhand_ros command.py --side left 0 0 1000 1000 1000 1000
# 兼容现有 50 Hz 发布端
ros2 topic pub -r 50 /dexhand_left/cmd std_msgs/msg/Int32MultiArray \
  '{data: [0, 0, 1000, 1000, 1000, 1000]}'
# 接受停止后禁止目标下发；phase 7 表示确认停止，8 表示异常
ros2 service call /dexhand_left/stop std_srvs/srv/Trigger '{}'
```

目标顺序为拇指侧摆、拇指屈伸、食指、中指、无名指、小指，六个整数范围 0..10000，单位为行程当量。长度或范围错误整条拒绝。QoS 为 Reliable、Volatile、KeepLast(1)。状态消息类型为 `roboparty_dexhand_ros/msg/DexHandState`，含与设计一致的阶段常量和六轴数组。

READY 收到首条有效目标后以配置频率（默认 200 Hz）重发最新缓存，不插值，不因上层退出而停止；state 以 50 Hz 发布。频率为调度目标，耗时超过周期时跳过落后的周期，不排队补发。实际频率受 SDK 和 CAN 总线负载影响。

stop 优先取消初始化后续步骤，并通过 SDK urgent stop 打断回零；连接期间等待连接操作结束再确认停止，不会继续使能。reset/stop 期间丢弃 cmd；停止后必须完整 reset。反馈过期、故障、使能或回零状态丢失会终止下发并尝试 stop，最终保持 ERROR。stop 不附加失能，不保证保持抓握。正常退出会尝试 stop 后断开；强制 kill、断电和断网无法保证物理停止。

## 最小验证

无需运行硬件测试。编译后启动 launch，确认两只手持续发布 `phase: 0`、`feedback_valid: false` 即可；启动阶段不会打开 CAN。本文中的 reset 和位置命令属于实际操作步骤，不属于无硬件验证。

2026-09-29 已在目标板完成 Release 编译和一次最小检查：双手未初始化状态发布实测均为 50 Hz；不存在的 CAN 接口上 reset 约 2 ms 返回接受，随后进入 ERROR，没有进入 READY。检查进程均已退出，未执行真实硬件连接、回零或运动；200 Hz 实机下发和运动中 stop 中断尚未实机验证。
