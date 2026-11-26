# Linux OTA 维护

`examples/python/ota.py` 是 SDK 随附的独立维护工具，复用此前无 SDK OTA 实现的 CANFD 升级协议。
需要 Linux、Python 3.9+ 和 `iproute2`，不需要安装 Python 包、编译 SDK 或启动 ROS。
可用于 x86_64 / aarch64 Linux，使用已经由操作系统配置好的 SocketCAN 接口。
工具随仓库和源码发布包提供，不包含在 Python wheel 的命令行入口中。

## 当前支持范围

- DH116S CANFD 左右手，节点 ID 为 `1..127`。`--interface` 和 `--node-id` 都必须显式填写。
- 正常升级要求当前设备版本严格大于 V1.20，即 V1.20 不支持，V1.21 及以后可进入升级流程。
- `--firmware` 可指定对应方向的其他版本固件，允许同版本重刷，不允许降级。
- 不指定路径时，按设备实际方向选择仓库内左手或右手 V1.22；当前版本高于 V1.22 时，需要
  显式提供同版本或更新固件。V1.22 是随附文件的版本，不是工具支持的版本上限。
- 型号、左右手、版本和序列号从设备读取，不根据接口名或节点 ID 推断左右手。
- 升级中断、应用身份无法读取时，可用 `upgrade --recover --firmware ...` 从头重刷。
  恢复模式不能读取并校验实际左右手、原应用版本和序列号，需要明确指定该设备对应的原始固件。
- 不配置 CAN 接口，不使能、回零或运动，不提供断点续传、自动回滚或数据批次自动重传。

左右手完整固件传输、节点 1/7/127 寻址、自定义版本和失败路径由离线模拟覆盖。
原独立工具的身份查询与通信测试已在本机左手上通过；此 SDK 集成版本的左右手、新版本固件
仍需在目标 Robo 板上进行完整实机升级验收。其他版本要求沿用相同的 CANFD OTA 协议。
离线测试和只读通信测试均不能替代真实 bootloader 的验证。

## 固件文件

仓库 `firmware/` 及源码发布包包含以下两份 CANFD V1.22 固件，各 278528 字节：

| 方向 | 文件 | SHA-256 |
| --- | --- | --- |
| 左手 | [DH116SL-CANFD-APP-H6E001-V1.22.upd](../firmware/DH116SL-CANFD-APP-H6E001-V1.22.upd) | `21f6ab15342844190bbe7e2f98e36a74bc5528408c327eca84ec682c18a1db37` |
| 右手 | [DH116SR-CANFD-APP-H6E001-V1.22.upd](../firmware/DH116SR-CANFD-APP-H6E001-V1.22.upd) | `9d1264863a0464f59a32445ccc91bdcc0e6e357febb1085d87b9f010b75daaac` |

来源为 2026-11-05 从[厂家固件发布页](https://zcnrg1e5hv0a.feishu.cn/wiki/WHvZwwQnKiEdbPkc8MzcaI2qn4A)
下载并整理的原文件。SHA-256 用于核对本地文件一致性，不是厂商签名。

指定其他固件时，应保留厂商原始文件名，例如 `DH116SR-CANFD-APP-H6E001-V1.23.upd`。
工具从文件名读取方向和版本，与设备信息核对；已知的两份 V1.22 文件还会核对 SHA-256。
其他版本只计算并记录 SHA-256，不声称已验证厂商校验值，也不限制为 V1.22 的文件大小。
文件名须符合 `DH116S[L/R]-CANFD-APP-<硬件标识>-V<主版本>.<两位次版本>.upd`，
硬件标识为大写字母或数字。文件必须非空，文件名最多 59 个 ASCII 字节，以适配 OTA 触发帧。
工具不解析 `.upd` 内部封装，文件名不能证明内容、硬件兼容性或厂商来源；请使用对应硬件的原始固件。

## 1. 准备目标和接口

停止使用目标接口的控制程序、ROS 节点和其他测试工具，确保手处于适合维护的静止状态。
确认供电、CAN H/L 接线和终端电阻正确。同一总线上不能存在两个使用相同节点 ID 的设备。
从仓库根目录运行，后续示例以 `can0`、节点 `1` 为例：

```bash
ip -details -statistics link show can0
```

开始操作前，工具检查接口存在、UP、MTU 72、开启 FD、非 loopback/listen-only，
以及仲裁/数据速率为 1 Mbps / 5 Mbps。配置不匹配时退出并报告原因，不调用 `sudo`，不修改接口。
`check`、`probe` 和 `upgrade` 都接受 ERROR-ACTIVE/ERROR-WARNING/ERROR-PASSIVE，
根据实际设备应答继续检查或升级。BUS-OFF 不接受，实际控制器状态会显示并记入日志。
Robo 板已配置好接口时直接进行下一步；否则按板端或适配器说明在系统中设置，
参考 [Linux 环境与 CAN 配置](getting-started.md#环境与-can-配置)。

实际采样点会显示并记入日志，不强制所有控制器采用同一值。此前 USB 适配器使用过 80%/75%；
这一经验不代表所有板端控制器的固定要求，实际配置需要在目标硬件上验证。

## 2. 检测

```bash
/usr/bin/python3 examples/python/ota.py check --interface can0 --node-id 1
```

检测依次检查接口，读取型号、左右手、版本及序列号，选择并校验固件，再以默认 10 Hz 进行
30 秒型号查询。正常路径只发送 SDO 读取；首次型号查询无应答时，补发一次只读 OTA 产品查询
`0x51`，判断 OTA 协议是否仍能响应。不发送升级触发、准备或固件数据，不调用普通 SDK 的 `Hand.connect()`。
通过时显示 `CHECK PASSED: Ready to upgrade.`，表示当前检查通过，可以对该目标继续执行升级。
报告中 `status` 为 `completed`、`success_rate` 为 `1.0`、`error` 为 `null`。
失败时显示 `CHECK FAILED: Normal upgrade readiness could not be confirmed` 及具体原因。
日志中的 `check_result.ready_to_upgrade` 同时记录这一结论；检查通过不代表已经升级或保证烧录成功。

通常约 300 次查询，实际次数受调度开销影响。要求查询全部成功，收发错误、丢包、溢出、
bus_error、bus_off 计数无新增；warning/passive 状态及次数变化只记录，不导致检查失败。
仲裁丢失及重启计数也仅记录。未提供的计数记为 `null`，不能视作零。
实际超时、异常应答或上述错误计数新增会导致失败，退出码为 1。

若首次型号查询无应答、但 OTA 产品查询有应答，输出 `OTA query responded; application identity is unavailable.`
并给出恢复命令，退出码为 2。日志记录 `recovery_available: true`、`ready_to_upgrade: false` 和
`finish.status: recovery_required`，表示可对此前确认的设备尝试恢复升级，不冒充正常检查通过。
OTA 有应答本身不能核验实际左右手、旧版本，也不能证明一定处于 bootloader。
`probe` 和正常 `upgrade` 在首次型号查询无应答后执行相同的只读诊断。两种查询都超时才报告均无应答。
若已收到型号应答，只是后续方向、版本或序列号读取超时，则报告身份读取不完整，不引导恢复升级。
可以增大 `--sdo-timeout-ms`（默认 500，最大 3000）再检查，以区分响应较慢和持续无应答。

可调整检查时长和频率，短检查仅适合诊断：

```bash
/usr/bin/python3 examples/python/ota.py check --interface can0 --node-id 7 --duration 60 --rate 10
```

升级其他版本时，检测和升级必须指定同一份固件。以下以右手 V1.23 为例，替换为实际路径：

```bash
/usr/bin/python3 examples/python/ota.py check --interface can0 --node-id 1 \
  --firmware /path/to/DH116SR-CANFD-APP-H6E001-V1.23.upd
/usr/bin/python3 examples/python/ota.py upgrade --interface can0 --node-id 1 \
  --firmware /path/to/DH116SR-CANFD-APP-H6E001-V1.23.upd
```

左右手不匹配、当前版本不满足要求、目标版本低于当前版本或固件校验失败时，检测失败。

## 3. 升级

检查通过后，在交互终端单独执行：

```bash
/usr/bin/python3 examples/python/ota.py upgrade --interface can0 --node-id 1
```

程序再次校验接口、目标身份和固件，显示当前版本、目标版本、固件文件名及序列号。
确认目标后按提示输入完整文字；左手示例：

```text
UPGRADE LEFT can0 1
```

右手对应 `UPGRADE RIGHT can0 1`，接口或节点不同，确认文字也随之改变。其他输入取消，
取消时不触发 OTA；程序返回 0 并记录
`cancelled`，不能据此判断升级成功。没有非交互 `--yes` 入口。
确认后再次核对接口索引和设备身份，发生热插拔或目标变化则退出。

默认 OTA 应答超时 1000 ms、包间隔 1 ms，与厂家 `main_ota` 一致。需要时可显式指定：

```bash
/usr/bin/python3 examples/python/ota.py upgrade --interface can0 --node-id 1 \
  --ota-timeout-ms 1000 --packet-gap-ms 1
```

握手最多尝试 10 次；`0x50` 收到文件错误 `0xEE` 时立即退出，其他未就绪状态继续尝试。
`0x53` 准备阶段首次超时、socket I/O 失败或应答尚未就绪时，关闭 CAN socket，等待
200 ms，再打开同一接口并重试，与厂家 `main_ota` 一致。
重连不要求控制器已恢复 ERROR-ACTIVE，也不重新配置接口。

所有命令都将控制器 warning/passive/恢复正常的通知记入 `can_state` 日志，继续等待设备应答，
这些通知不会延长应答超时。BUS-OFF、缓冲区溢出等其他 CAN 错误仍会停止操作；
收到状态通知不代表某批数据已写入。
固件每包 32 字节，每批最多 16 包，必须收到匹配的 `AA/5A` 或 `AB/5A` 应答才继续。
按厂家流程发完一批再等待应答，普通批次等待 `AA` 时忽略迟到的其他命令回包；
最终批次严格要求 `AB/5A`。
批次应答超时或内容异常立即停止，不自动重发数据。成功必须出现最终应答：

```text
Transfer complete: final AB/5A ACK received. Run probe after device restart.
```

升级期间保持供电和连接，不启停 CAN、不关闭终端。中途失败时保持供电、保留日志。

### 升级中断后恢复

厂家 `main_ota` 允许应用初始化无应答后继续 OTA。对应本工具的 `--recover` 模式，
跳过应用 SDO 身份读取，复用 `0x50` 文件名、`0x51` 产品码、`0x52` 版本、`0x53` 准备的
完整握手，再从第 0 包重新发送整个文件；收到准备成功应答前不发送固件数据。
这不是断点续传，也不是自动回滚。

仅用于此前已确认是受支持设备、升级中断后需要恢复的场景。恢复时必须提供对应设备的原始
固件路径，不根据 bootloader 产品码或节点 ID 猜左右手，也不自动选择随附固件。
由于读不到原应用版本，恢复模式无法自动校验原版本 > 1.20 或阻止降级，应使用中断前的
同一份升级文件。仍执行本地文件名、非空及已知 V1.22 SHA-256 校验。

例如已确认是 `can0`、节点 `1` 的左手，本次中断的目标为 V1.22：

```bash
/usr/bin/python3 -B examples/python/ota.py upgrade --interface can0 --node-id 1 \
  --recover --firmware firmware/DH116SL-CANFD-APP-H6E001-V1.22.upd
```

程序会显示 `Flash: unknown -> 1.22`，并说明左右手来自所选文件，当前版本和序列号未经核验。
核对目标后输入：

```text
RECOVER LEFT can0 1
```

右手选择 `DH116SR-...upd`，确认文字为 `RECOVER RIGHT ...`。取消确认不会发送 OTA 指令。
正常 `upgrade` 不会因一次 SDO 超时自动开始刷写。`check` 和 `probe` 可报告应用身份或 OTA
可达性；收到恢复提示后无需反复检查，恢复成功并按设备要求重启后再执行 `probe`。

## 4. 升级后复查

完整传输成功后，按设备要求重启，再运行只读身份查询：

```bash
/usr/bin/python3 examples/python/ota.py probe --interface can0 --node-id 1
```

确认 `DH116S Left` 或 `DH116S Right`、序列号与原设备一致，且版本与所选固件一致。
`probe` 不需要固件文件，也不会进行 30 秒测试。同版本重刷前后版本相同，
应同时保留这一次最终成功应答的记录。

## 文件和互斥

默认固件相对脚本定位到仓库 `firmware/`，与当前工作目录无关；`--firmware` 接受绝对或相对路径。
OTA 握手传入所选文件的实际文件名，因此不能随意重命名厂商固件。

日志默认写入当前工作目录下的 `logs/ota/`。可用 `--log-dir /writable/path` 指定可写位置。
日志包含接口、节点、采样点、身份、固件校验值、收发帧、健康检查结果、最终应答及退出状态。
操作通常不需要 sudo；若权限受板端策略限制，按部署要求授予访问权限。

新版 Linux SDK 与维护脚本共享 `/tmp/dh116s-ota-<interface>.lock`：普通 SDK 会话持有共享锁，
维护脚本持有独占锁。SDK 已占用接口时维护会被拒绝；维护期间 SDK 连接也会被拒绝。
正常结束、异常退出或进程结束都会释放锁；锁文件保留，不要手动删除正在使用的锁文件。
这也兼容原独立 OTA 工具的锁名。

锁只约束使用同一约定、位于同一文件系统环境中的程序。旧版 SDK、厂商工具及任意自定义程序
不一定遵守锁；脚本不扫描全机进程名，避免阻塞使用其他 CAN 接口的程序。
重新编译或安装本次 SDK 后控制程序才具有新的互斥保护，仍需先停止相关程序。

## 离线验证

```bash
/usr/bin/python3 -B tests/test_ota.py -v
cmake -S . -B build/check -DRPHAND_BUILD_TESTS=ON
cmake --build build/check --parallel 2
ctest --test-dir build/check --output-on-failure
```

Python 测试使用模拟对端及 AF_UNIX socket，不创建 AF_CAN socket。直接运行 Python 测试时，
C++ 互斥集成测试会跳过；Linux CTest 自动传入编译后的辅助程序，验证双方的共享/独占锁冲突
和退出后释放。测试还覆盖不同节点、左右手各完整 8704 包传输、其他版本、固件校验、
确认取消、身份变化、准备阶段状态通知或未就绪应答后重连、恢复通知后的数据传输、
应用无应答时左右手完整恢复升级、迟到握手回包、超时不重发、错误应答、队列溢出、
warning/passive 状态下的 30 秒检查、应用超时后的只读 OTA 诊断、配置检查及检测命令只读性。

协议中 `0x2030:0x05` 的原始方向值为 `1=右手、2=左手`，与 SDK 公共枚举值不同。
OTA `0x53` 的参数 `0x02` 表示 CAN 传输，不是方向；bootloader 的产品码和版本原值仅记录，
当前版本判断使用应用的 SDO 版本字段，不混用这两类字段。
