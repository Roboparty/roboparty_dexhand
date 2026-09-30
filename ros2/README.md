# roboparty_dexhand_ros

DH116S ROS 2 Jazzy 驱动，C++ 节点复用仓库内 SDK，Python 提供 launch 和上层示例。

在完整 SDK 仓库根目录：

```bash
bash build.sh ros
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 launch roboparty_dexhand_ros dual_hand.launch.py command_rate_hz:=200
```

启动不自动回零；reset 会驱动整手。参数、左右手命令、状态机、wave 和 RP1 手柄适配统一见 [ROS 指南](../docs/ros2.md)。其他构建方式见 [构建指南](../docs/build.md)。
