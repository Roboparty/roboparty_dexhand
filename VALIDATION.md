# RP_Hand 新板部署与验收 SOP

适用于已经安装 `roboparty-dexhand`、CAN-FD 接口已经由系统配置完成的
RP_Hand 6DOF。本文从反馈周期固化开始，不负责配置 CAN 接口。

开始前确认：

- 手已上电，接头、供电和终端电阻正常；
- 当前没有其他灵巧手控制进程；
- 已取得真实硬件运动授权，手周围无人且没有障碍物；
- 下文的 `canX` 是占位符，必须替换为手实际连接的 `can0`、`can1`、
  `can2` 或 `can3`。

## 第 3 步：反馈周期一次性固化

这一步将完整反馈调整为 `50 Hz`。两个反馈帧类型各发送 `50` 帧/秒，
总线上合计约 `100` 帧/秒。仅新手、换手、恢复出厂或配置未知时需要执行，
日常启动不需要重复。

### 3.1 写入并保存

将下面两处 `canX` 替换为实际接口后执行：

```bash
source /opt/roboparty/setup.bash
roboparty-dexhand-config feedback-period apply \
  --interface canX --node-id 1 --milliseconds 20 --save
```

正常结果：

- `result=saved`：六轴参数已经写入并保存；
- `result=already-compliant`：六轴本来就是 `200`，未重复写入；
- `result=read-failed`、`result=save-failed` 或其他失败结果：转到
  “异常处置 A”。

### 3.2 手本体断电重启

无论结果是 `saved` 还是 `already-compliant`，都只给 **RP_Hand 手本体**
断电再上电，不要用重启开发板代替。重新上电后等待约 5 秒。

反馈周期由手在上电时加载；`init_hand()` 不会写入或修改这个持久化参数。

### 3.3 读回确认

```bash
roboparty-dexhand-config feedback-period show --interface canX --node-id 1
```

判据：输出包含 `result=shown`，并且六个轴的 raw value 全部为 `200`。
任一轴不是 `200`，都不能进入运动验收。

## 第 4 步：环境与功能验收

### 4.1 Python API 冒烟测试

```bash
source /opt/roboparty/setup.bash
python3 -c 'from dexhand_py import HandDriver, HandModel; print(HandModel.RP_HAND_6DOF.value)'
```

判据：输出 `0`，且没有 `ImportError`。

### 4.2 六轴运动、位置跟踪与帧率

下面命令会初始化、使能并正常回零，然后让六个轴先运动到 `1200`，读取位置，
再回到 `0` 并再次读取位置。执行前将代码中的 `canX` 替换为实际接口。

真实硬件会运动。执行前确认工作空间安全并准备好随时切断手本体电源。
脚本读取的 `rx_packets` 是整个 CAN 接口的总帧率；只有总线上没有机械臂等
其他设备流量时，才能用它估算灵巧手的约 `100 fps`。共享总线场景以第 3 步
六轴 raw value 全部为 `200` 作为反馈周期判据。

```bash
source /opt/roboparty/setup.bash
python3 - <<'PY'
import time

from dexhand_py import HandDriver, HandModel


CAN_INTERFACE = "canX"
NODE_ID = 1


def positions(hand):
    return [hand.get_now_position(joint) for joint in range(1, 7)]


hand = HandDriver.create_hand(
    "RP_Hand", "canfd", CAN_INTERFACE, HandModel.RP_HAND_6DOF, NODE_ID
)

try:
    if not hand.init_hand(True, True, 5.0):
        raise RuntimeError("init_hand failed; motion is forbidden")

    hand.check_health()
    total, active = hand.get_dof()
    if active != 6:
        raise RuntimeError(f"expected active DOF 6, got {active} (total={total})")

    alarms = [hand.get_now_alarm(joint) for joint in range(1, 7)]
    if any(alarm != 0 for alarm in alarms):
        raise RuntimeError(f"joint alarms must all be zero: {alarms}")

    rx_path = f"/sys/class/net/{CAN_INTERFACE}/statistics/rx_packets"
    with open(rx_path, encoding="ascii") as stream:
        before = int(stream.read())
    time.sleep(3.0)
    with open(rx_path, encoding="ascii") as stream:
        after = int(stream.read())
    print(f"feedback rate: {(after - before) // 3} fps")

    for joint in range(1, 7):
        hand.set_target_position(joint, 1200)
        hand.set_position_velocity(joint, 2000)
    hand.move_motors(0)
    time.sleep(1.5)
    print("target 1200:", positions(hand))

    for joint in range(1, 7):
        hand.set_target_position(joint, 0)
        hand.set_position_velocity(joint, 2000)
    hand.move_motors(0)
    time.sleep(1.5)
    print("target 0:", positions(hand))
finally:
    hand.deinit_hand()
PY
```

判据：

- 总线没有其他流量时，`feedback rate` 约为 `100 fps`，现场可将
  `80..120 fps` 视为正常波动；共享总线不使用该项判定；
- `target 1200` 输出六个有效位置，且能够跟随目标方向运动；
- `target 0` 输出六个接近零位的位置；
- 程序正常退出，没有异常。

全部满足即表示 RP_Hand 已可正常使用。

## 异常处置

### A. `apply` / `show` 无应答或失败

按顺序检查：

1. 确认已经把所有 `canX` 替换为实际接口名；
2. 重新插紧手的末端接头，确认手本体供电和指示灯正常；
3. 确认 node ID 为 `1`，并停止其他灵巧手控制进程；
4. 查看接口收发与错误计数：

   ```bash
   ip -details -statistics link show canX
   ```

5. 若接口正常但仍无应答，将供电、接线、终端电阻和 CAN 适配器状态交给
   硬件或嵌入式人员继续排查。

`candump` 中看到自己发送的帧可能只是本地回显，不能单独证明物理设备已经应答。

### B. 帧率约 `2000 fps`

1. 再次运行 `feedback-period show`，确认六轴 raw value 是否全部为 `200`；
2. 若不是，重新执行第 3 步；
3. 若已经全部为 `200`，确认参数保存后确实给手本体断电重启过；
4. 仍不正常时联系固件或厂商支持人员。

### C. 位置全为 `0` 或不跟随

1. 复核 `create_hand()` 使用的接口名和 node ID；
2. 检查六轴报警：`get_now_alarm(1..6)`；
3. `init_hand()` 返回 `False` 时禁止继续发送运动命令，先按异常处置 A 排查；
4. 确认没有其他进程同时控制同一只手。
