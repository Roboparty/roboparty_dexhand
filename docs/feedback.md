# Linux SDO 参数与反馈周期配置

`examples/python/feedback.py` 使用 Python 标准库和同目录的 `ota.py`，无需安装 SDK 或 ROS。
CAN 接口须已配置为 CAN FD 1 Mbps / 5 Mbps。脚本不使能、不回零、不发送运动目标。
使用前退出 SDK、ROS、`wave_demo` 和其他控制程序。

## 四个功能

在仓库根目录执行，默认接口 `can0`、节点 `1`：

```bash
# 读取六轴 SDO 周期寄存器，不测量反馈
/usr/bin/python3 -B examples/python/feedback.py read-sdo

# 设置六轴 SDO 周期为 20 ms，逐轴读回核对
/usr/bin/python3 -B examples/python/feedback.py set-sdo 20

# 被动测量两组实际反馈周期及频率，不发送配置或 SDO 请求
/usr/bin/python3 -B examples/python/feedback.py read-feedback

# 设置两组反馈各每 20 个基础周期上报一次，然后测量
/usr/bin/python3 -B examples/python/feedback.py set-feedback 20
```

所有命令支持 `--can can1 --id 2`，也兼容 `--interface`、`--node-id`。
原来的 `read`/`set` 混合命令已取消；SDO 和反馈分别操作，不再使用 `--register-ms` 或 `--divider`。

## 两个参数的区别

| 命令 | 参数含义 | 确认方式 |
| --- | --- | --- |
| `set-sdo MS` | 六轴 `0x201d/205d/209d/20dd/211d/215d:0x14`，按协议 0.1 ms 单位编码 | 写入后逐轴 SDO 读回 |
| `set-feedback N` | `0x50` 位置组、`0x5A` 状态组各每 N 个基础周期上报一次，范围 1..255 | 无已知分频读回接口，以实际收包测量 |

SDO 参数不能当作实际上传周期，也不能直接与分频相乘。当前右手 V1.22 的实测中，
寄存器为 200（20 ms），分频为 1 时仍有约 1 ms 的接收间隔；分频为 20 时两组各约 50 Hz。
不同固件的基础周期应实测确认，不能保证 N 永远等于 N 毫秒。

当前 SDK 连接时会先将六轴 SDO 写为 200，再发送 `00 04 50 14 5A 14`，
即设置分频为 20，本机两组各约 50 Hz。重新启动 SDK 或 `wave_demo` 会覆盖脚本的运行时配置。

## SDO 参数及 EEPROM 保存

```bash
# 只读取第 2 轴
/usr/bin/python3 -B examples/python/feedback.py read-sdo --axis 2

# 只设置第 2 轴为 5 ms，不保存
/usr/bin/python3 -B examples/python/feedback.py set-sdo 5 --axis 2

# 设置六轴为 20 ms，读回核对后发送 EEPROM 保存命令
/usr/bin/python3 -B examples/python/feedback.py set-sdo 20 --save
```

`MS` 范围为 0.1..6553.5，必须为 0.1 ms 的整数倍；设备支持范围以 Abort/读回结果为准。
默认 SDO 单次等待 500 ms，最多尝试 3 次；可用 `--sdo-timeout-ms 2000 --attempts 3` 修改。
仅超时重试，收到 Abort 立即停止。写 ACK 丢失时，必须读回匹配才视为运行时写入核验通过。
SDO 命令不要求设备正在上报反馈，也不修改反馈分频；高负载时可先单独执行 `set-feedback 20`。

`--save` 仅供 `set-sdo` 使用，并要求全部六轴（不传 `--axis` 或传 `--axis 0`）。
全部写入及读回成功、CAN 错误计数检查通过后，向 `0x600 + node` 发送厂商序列：

```text
00 10 10 00 00 00 00 00
间隔至少约 5 ms
2F 10 10 01 73 61 76 65
```

此序列来自本地厂商协议及四版 SDK 逆向记录：
`/home/wsy/data/cwl/code/DH116S灵巧手/DH116S手册整理/DH116S永久反馈周期配置分析.md`。
第二帧使用厂商实际格式，不替换成通用 CANopen 写格式。
保存序列只发送一次、不自动重试；收到 `0x1010` Abort 或总线错误时停止。
发送后监听至少 1 秒，再核对六轴运行时读回；这段等待不是已验证的 EEPROM 写入完成时间。

**发送成功、ACK 或运行时读回一致，都不能证明断电持久化完成。** 脚本只报告保存序列已发送，
并将日志的 `persistence_verified` 保持为 false。保存可能包含其他已修改的驱动参数，
不是只保存反馈周期。不要在每次连接或循环控制中反复保存；失败时可能已经部分生效。

确认设备保存完成后，断电重启，在启动任何 SDK/`wave_demo` 之前执行：

```bash
/usr/bin/python3 -B examples/python/feedback.py read-sdo
/usr/bin/python3 -B examples/python/feedback.py read-feedback
```

分别检查寄存器是否保留、实际反馈频率是否符合目标。**SDO 参数保存不保证反馈分频也会保存，
因此不能将 `set-sdo 20 --save` 称为永久设置 50 Hz。** 目前没有已确认的分频持久化命令。

## 反馈测量与配置

```bash
# 延长实际收包测量到 10 秒
/usr/bin/python3 -B examples/python/feedback.py read-feedback --duration 10

# 本机分别约为每组 100 Hz、200 Hz；恢复 SDK 当前分频为 20
/usr/bin/python3 -B examples/python/feedback.py set-feedback 10
/usr/bin/python3 -B examples/python/feedback.py set-feedback 5
/usr/bin/python3 -B examples/python/feedback.py set-feedback 20
```

`set-feedback N` 发送 `00 04 50 N 5A N`，将反馈内容配置为位置和状态两组，
不是对其他未知组增量修改。不写 SDO、不保存 EEPROM，退出时不自动恢复。
命令没有已知 ACK/分频读回；测到数据不代表设备确认了精确分频值。

测量输出各组帧数、Hz、平均间隔、P50/P95 和最大间隔，间隔单位 ms。
这些是主机接收时间，USB 批量交付和调度会影响间隔，不等同于电机采样或物理 CAN 时间戳。
默认先丢弃 1 秒数据，可用 `--settle 3` 修改。反馈不足时返回失败，不会自动开启反馈。

## 日志与测试

日志默认写入 `logs/feedback/`，可用 `--log-dir` 修改。日志记录 SDO、配置/保存报文、
超时、读回、测量及 CAN 计数变化，不逐帧记录周期反馈。四个命令都使用 SDK/OTA 同一维护锁，
旧 SDK 或其他工具不一定遵守此锁，仍需退出其他控制程序。

离线测试不打开硬件：

```bash
/usr/bin/python3 -B tests/test_feedback.py -v
```
