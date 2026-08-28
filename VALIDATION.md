# RP_Hand 6DOF ARM64 软件包硬件验收

本文用于已安装的 ARM64 `roboparty-dexhand` 软件包的现场验收。所有命令均
连接到真实硬件；执行初始化或运动命令前必须取得运动授权。

## 安全前提

- 已取得本次真实硬件运动的授权，且现场操作员全程在场；
- 手周围工作空间已清空，人员、工具和障碍物均不在运动范围内；
- 手本体供电、CAN-FD 接线和总线终端电阻均已按现场方案正确连接；
- 本文的免回零运动要求手已处于已知的参考/零位状态。参考状态未知时，
  不得继续执行免回零运动。

CAN-FD 接口由系统或部署脚本在库外准备，本库不配置接口状态、速率、接线或
终端。先在目标设备的 shell 中执行：

```bash
source /opt/roboparty/setup.bash
export CAN_INTERFACE=can3
export NODE_ID=1
ip -details -statistics link show "$CAN_INTERFACE"
```

仅将 `CAN_INTERFACE` 的值改为实际接口 `can0`、`can1`、`can2` 或 `can3` 中
的一个，并按现场分配修改 `NODE_ID`（有效范围为 `1..127`）。这是本文唯一的
接口和 node ID 赋值处；完成后保持同一个 shell 执行全部验收步骤。若关闭或更换
shell，必须重新执行本准备块，不能依赖默认值或先前 shell 的变量。输出必须显示
接口为 `UP`、名义速率 `1M`、数据速率 `5M`，且启用 FD；不满足时由系统或部署
脚本修正后再继续。

## 反馈周期

在没有其他手控制进程运行时，应用并保存 20 ms 反馈周期：

```bash
(
  : "${CAN_INTERFACE:?先执行准备步骤}"
  : "${NODE_ID:?先执行准备步骤}"
  roboparty-dexhand-config feedback-period apply --interface "$CAN_INTERFACE" --node-id "$NODE_ID" --milliseconds 20 --save
)
```

若输出为 `result=already-compliant`，当前六个轴已经符合要求，CLI 不会重复写入
或保存，可以跳过闪存写入。无论成功结果是否为 `already-compliant`，都进入以下
明确的电源边界：**只对 RP_Hand 手本体断电再上电，不要用控制板或机器人主板
重启代替。**

手本体重新上电后，在同一个 shell 中执行以下命令块确认持久化配置；如果该 shell
已关闭，先重新执行本文的准备块：

```bash
(
  : "${CAN_INTERFACE:?先执行准备步骤}"
  : "${NODE_ID:?先执行准备步骤}"
  roboparty-dexhand-config feedback-period show --interface "$CAN_INTERFACE" --node-id "$NODE_ID"
)
```

`show` 输出的六个轴 raw value 必须全部为 `200`；任何一个轴不是 `200` 都不能
进入运动验收。

## Python API 识别

在当前 shell 中确认已安装 Python API 的公开模型数值：

```bash
source /opt/roboparty/setup.bash
python3 -c 'from dexhand_py import HandModel; print(HandModel.RP_HAND_6DOF.value)'
```

输出必须恰好为 `0`。不要打印枚举对象或名称，只使用上述 `.value` 数值检查。

## 受控运动

再次目视确认手正处于已知的参考/零位状态，工作空间仍已清空，并且操作员已取得
运动授权。`init_hand(True, False, 0.0)` 会在返回前启用电机并启用免回零运动，
因此必须在初始化前完成确认。下面命令块在独立子 shell 中运行，会显示当前接口
和 node ID，并从终端要求操作员精确输入 `MOVE`；其他任何输入均会中止，且不会
创建或初始化驱动。若关闭了准备步骤所在的 shell，先重新执行准备块：

```bash
(
  : "${CAN_INTERFACE:?先执行准备步骤}"
  : "${NODE_ID:?先执行准备步骤}"
  printf 'CAN_INTERFACE=%s NODE_ID=%s\n' "$CAN_INTERFACE" "$NODE_ID"
  ip -details -statistics link show "$CAN_INTERFACE" || exit 1
  if ! read -r -p '已确认参考/零位和工作空间；输入 MOVE 继续: ' confirmation </dev/tty; then
    printf '未读取到 MOVE 确认，已中止。\n' >&2
    exit 1
  fi
  if [[ "$confirmation" != "MOVE" ]]; then
    printf '确认不是 MOVE，已中止。\n' >&2
    exit 1
  fi
  python3 - <<'PY'
import os
import sys
import time

from dexhand_py import HandDriver, HandModel


def print_positions(hand, label):
    positions = [hand.get_now_position(joint) for joint in range(1, 7)]
    values = " ".join(
        f"joint{joint}={position}"
        for joint, position in enumerate(positions, start=1)
    )
    print(f"{label}: {values}")


can_interface = os.environ["CAN_INTERFACE"]
try:
    node_id = int(os.environ["NODE_ID"])
except ValueError as error:
    raise RuntimeError("NODE_ID must be an integer in 1..127") from error
if not 1 <= node_id <= 127:
    raise RuntimeError("NODE_ID must be in 1..127")

hand = None
initialized = False
bypass_enabled = False
primary_error = None
try:
    hand = HandDriver.create_hand(
        "RP_Hand", "canfd", can_interface, HandModel.RP_HAND_6DOF, node_id
    )
    if hand is None:
        raise RuntimeError("create_hand returned no driver")
    if not hand.init_hand(True, False, 0.0):
        raise RuntimeError("init_hand failed")
    initialized = True
    bypass_enabled = True

    hand.check_health()
    total, active = hand.get_dof()
    if active != 6:
        raise RuntimeError(f"expected active DOF 6, got {active} (total={total})")
    alarms = [hand.get_now_alarm(joint) for joint in range(1, 7)]
    if any(alarm != 0 for alarm in alarms):
        raise RuntimeError(f"joint alarms must all be zero: {alarms}")

    for joint in range(1, 7):
        hand.set_target_position(joint, 5000)
        hand.set_position_velocity(joint, 3000)
    hand.move_motors(0)
    time.sleep(3.0)
    print_positions(hand, "target5000")

    for joint in range(1, 7):
        hand.set_target_position(joint, 0)
        hand.set_position_velocity(joint, 3000)
    hand.move_motors(0)
    time.sleep(3.0)
    print_positions(hand, "target0")
except BaseException as error:
    primary_error = error
    raise
finally:
    cleanup_failures = []
    if hand is not None:
        if initialized:
            try:
                hand.stop_motors(0)
            except BaseException as error:
                cleanup_failures.append(("stop_motors", error))
        if bypass_enabled:
            try:
                hand.set_move_no_home(0)
            except BaseException as error:
                cleanup_failures.append(("set_move_no_home(0)", error))
        try:
            hand.deinit_hand()
        except BaseException as error:
            cleanup_failures.append(("deinit_hand", error))
    if cleanup_failures:
        print("严重：清理失败，手可能仍处于不安全状态：", file=sys.stderr)
        for operation, error in cleanup_failures:
            print(f"  {operation}: {error!r}", file=sys.stderr)
        print("请立即移除 RP_Hand 手本体电源，且不要继续测试。", file=sys.stderr)
    if primary_error is None and cleanup_failures:
        details = "; ".join(
            f"{operation}: {error!r}" for operation, error in cleanup_failures
        )
        raise RuntimeError(f"cleanup failed: {details}")
PY
)
```

初始化成功后，脚本将 `bypass_enabled` 记录为真，以反映初始化已启用的免回零状态。
在健康检查通过、活动 DOF 为 `6` 且关节 `1..6` 报警全为 `0` 前，脚本不会发送
任何目标位置或目标速度命令。`total` DOF 可能为 `11`，验收以活动 DOF 为准。清理
阶段会在已初始化时尝试 `stop_motors(0)`，在已启用免回零时恢复
`set_move_no_home(0)`，并且只调用一次 `deinit_hand()`；所有清理失败都会逐项输出
到 `stderr`，且不会覆盖先前发生的主错误。出现“严重：清理失败”警告时，必须立即
移除 RP_Hand 手本体电源，并且不要继续测试。

## 验收清单

- 所选接口显示为 `UP`，并具备 `1M` 名义速率、`5M` 数据速率和 FD；
- `feedback-period apply` 与重新上电后的 `show` 均成功；
- `show` 的六个 raw value 全部为 `200`；
- Python 模型数值输出恰好为 `0`；
- `target5000` 和 `target0` 各输出六个位置，且位置接近相应目标，按硬件验收
  容差判定，不在本文另行虚构固定阈值；
- 脚本清理并退出，无未处理错误，也没有“严重：清理失败”警告；若出现该警告，立即
  移除 RP_Hand 手本体电源且不要继续测试。
