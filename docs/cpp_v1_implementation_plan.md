# DH116S C++ SDK 第一版实现方案（待审阅）

本文只描述第一版公共接口的内部实现方案、厂家 SDK 映射、线程组织和验收方法。公共接口的作用、字段和返回值以 [第一版接口说明](v1_api_summary.md) 为准；本文件不再重新定义用户侧语义。仅做设计，不表示已经实现、编译或实机验证。

实现层必须遵守两个边界：第一，厂家头文件和原始错误码不能进入公共头文件；第二，内部可以等待反馈或有限重试，但不能改变公共接口的返回值含义。第一版不使用 C++ 异常，失败通过 `bool`、状态字段和日志报告。

## 1. 范围与依据

先实现 Linux SocketCAN CAN FD 下的 C++ SDK，位置使用 0～10000 行程当量；Python 和 ROS 2 本阶段不实现。不增加设备信息查询、通道信息查询、故障清除、角度控制、轨迹规划和触觉接口。

依据按以下顺序交叉核对：

1. 本地厂家 2026-09-21 包的 `LHandProLib.hpp`：实际函数签名、线程约束和缓存行为。
2. 同版本 `examples/CANFD_cpp/main.cpp`：回调类型、初始化和清理流程。
3. CAN FD 协议：帧结构、状态位、SDO 索引和反馈周期设置。
4. 上位机手册：力矩到位停止与保持的行为说明。
5. `arm_sdk_minimal/hand.hpp`、`dual_runtime.hpp`：现有 SocketCAN、多实例和反馈确认实现参考。

现有示例不是完整公共 SDK。尤其不要直接继承其自动使能回零、固定两实例回调路由、最多 50 次重试和 1 秒反馈过期阈值。它们是测试程序的策略，不是本 SDK 已确定的行为。

## 2. 公共接口到厂家接口的映射

| 公共接口 | 主要厂家接口 | 额外底层工作 |
| --- | --- | --- |
| `connect(can, node_id, retry_count = 30)` | `set_send_canfd_callback_ex()`、`initial(LCN_CANFD, node_id)`、`get_hand_type()`、`get_dof()`、SDO 读写 | SocketCAN 打开/收发；分阶段有限重试；六轴 20 ms 周期配置；上报帧配置；实测 50 Hz |
| `disconnect()` | `close()` | 无参数、无连接重试；取消待执行请求和操作等待，协调当前调用结束，回收线程和 socket；不自动停止或失能 |
| `enable()` | `set_control_mode(0, LCM_POSITION)`、`set_move_no_home(0)`、`set_enable(0, 1)` | 内部等待新的状态反馈；六轴全部确认使能才返回 `true`，否则 `false` |
| `disable()` | `set_enable(0, 0)` | 取消待发运动和旧操作；六轴全部确认失能才返回 `true`，否则 `false` |
| `home()` | `home_motors(0)`；必要时 `home_motors(id)`；完成后 `set_control_mode(0, LCM_POSITION)` | 固定每轴最多 30 次按轴补发；跟踪本次回零状态，处理超时和中断；成功返回六轴完成数组 |
| `set_positions(q)` | 六次 `set_target_position(id, q)`，一次 `move_motors(0)` | 薄封装，按顺序直调厂家接口；不做设备状态检查、不等待、不重试、不保证执行成功 |
| `set_position(id, q)` | `set_target_position(id, q)`、`move_motors(id)` | 只检查 ID/位置输入，直接调用单轴厂家接口；不检查返回、不等待、不保证执行 |
| `set_speed(v)` / `get_speed()` | `set_position_velocity(0, v)`、`apply_motion_params()`、`get_position_velocity(id, &v)` | 设置最多重试 30 次；读取厂家缓存数组，不是设备 ACK |
| `set_current_limit(c)` / `get_current_limit()` | `set_max_current(0, c)`、`apply_motion_params()`、`get_max_current(id, &c)` | 设置最多重试 30 次；读取厂家缓存数组，不是设备 ACK |
| `set_torque_reached_mode(mode)` / `get_torque_reached_mode()` | `set_safe_current_enable(mode)`、`get_safe_current_enable(&mode)` | 1=安全电流开启/力矩到位停止；0=关闭/力矩到位保持；设置最多重试 30 次 |
| `get_state()` | 接收时调用 `get_now_position()`、`get_now_position_velocity()`、`get_now_current()` | 接收缓存、状态位解析、新鲜度判断；查询时不发包 |
| `stop()` | `stop_motors(0)` | 取消待发运动和回零等待；六轴都确认停止才返回 `true`，否则 `false` |

说明：`get_hand_type()` / `get_dof()` 仅用于连接内部校验，不重新增加公共 `get_info()` / `get_joint_info()`。SDK 应确认匹配 DH116S、六主动轴后再允许控制。

## 3. 建议的内部组织

### 3.1 文件与依赖

```text
include/dh116s/hand.hpp       公共 Hand 类、固定六通道数据结构、返回类型
src/hand.cpp                生命周期、接口逻辑、操作等待
src/vendor_adapter.*        厂家调用、回调、返回码转换
src/socketcan_transport.*   SocketCAN 收发与过滤
src/feedback.*              状态缓存、时间与帧率检查
tests/                     无硬件验证和显式运行的硬件测试
```

建议 C++17 起步，公共数组用 `std::array<int, 6>`；厂家头文件放在实现层。构建时用 `LHANDPRO_SDK_ROOT` 指向匹配架构的厂家包，不把当前机器绝对路径写入公共头文件。暂不把整个厂家附件目录复制进仓库。

### 3.2 每只手的线程和资源

每个 Hand 拥有一个厂家对象、一条控制工作线程、一条接收线程、独立 socket 和反馈缓存。

- 厂家明确要求配置和控制 setter 来自同一个用户线程。因此所有公共控制请求交给同一工作线程执行；仅用 mutex 让不同调用线程轮流调用厂家 setter 不满足这条要求。
- 接收线程可并发调用 `set_canfd_data_decode()`；它不能等待控制线程才能处理初始化和 SDO 应答。
- `get_state()` 只复制本地缓存，使用短时间缓存锁；任何等待反馈的操作都不能持有该锁。
- `home()` 可以阻塞用户调用线程，但工作线程将其登记为进行中的操作，继续处理接收事件、期限和停止请求。
- `stop()`、`disable()`、`disconnect()` 优先于普通运动请求，取消此前尚未发送的目标。建议普通运动不建立长队列：已有未完成下发请求时返回 Busy；用户在每次下发返回后提交下一目标。
- 停止前已进入厂家调用的操作不能强制打断；停止在该调用返回后执行。不要宣称硬实时抢占。

多实例按 `(CAN 接口, node_id)` 区分。不同总线上可以使用相同 node_id，同一总线上应使用不同 node_id。进程内拒绝重复控制同一设备；对其他进程不承诺自动仲裁。

### 3.3 回调和底层收发

使用厂家示例中真实的四参数回调，不照抄头文件注释里过时的双参数示例：

```cpp
// 保存在实例的稳定地址，生命周期覆盖厂家对象及其全部后台回调。
std::function<bool(unsigned, const unsigned char*, unsigned, int)> send_callback;
send_callback = [transport](id, data, size, extended) {
    return transport.send(id, data, size, extended);
};
vendor.set_send_canfd_callback_ex(&send_callback);
```

底层 `send()`：检查长度不超过 64，将 CAN FD 长度补齐到合法 DLC 对应长度，填充字节清零，设置帧 ID 和 FD/BRS 标记，调用 `write()`，检查完整帧是否成功交给内核。失败记录 errno 和发送错误序号，并通过回调返回 false；内核接受不等于设备执行。

底层 `receive_loop()`：

```text
poll(socket, 唤醒通道)
read(canfd_frame)
校验实际读取长度、CAN/FD 数据长度和帧标记
按 node_id 接收 0x480+node_id 的过程反馈、0x580+node_id 的 SDO 应答
SDO 应答先送入等待中的事务匹配器
将支持的帧交给 vendor.set_canfd_data_decode(id, payload, size)
仅对结构合法、成功解码的必要过程反馈更新缓存和时间
通知操作等待者
```

SocketCAN 使用 `if_nametoindex()`、`socket(PF_CAN, SOCK_RAW, CAN_RAW)`、`setsockopt(CAN_RAW_FD_FRAMES)`、`setsockopt(CAN_RAW_FILTER)`、`bind()`、`poll()`、`read()`、`write()`、`close()`。发送需有有限超时；接收有唤醒机制以便退出。拒绝错误帧、RTR 帧以及与当前协议不匹配的扩展帧进入正常数据解码。

连接只使用已经配置并启用的 CAN FD 网络接口，不执行提权命令、修改主机总线波特率或关闭其他设备程序。此处“配置帧率”指设备反馈周期，不是主机 CAN 比特率。

## 4. connect(can, node_id, retry_count = 30)（已确认）

### 4.1 输入和成功条件

入口只包含 CAN 接口、节点 ID 和内部重试次数：

```cpp
hand.connect("can0", 1);      // 每个阶段最多尝试 30 次
hand.connect("can0", 1, 50);  // 每个阶段最多尝试 50 次
```

`retry_count` 包含第一次尝试，默认 30，即最多首次尝试加 29 次重试。初始化阶段和反馈配置检查阶段各自计数，不共用一个总次数，也不对每个轴再嵌套 retry_count 次重试。

反馈固定为每类 50 Hz、周期 20 ms，不开放频率和超时参数。每次等待应答、配置检查的期限及重试间隔作为内部参数，具体数值待实测确定。节点范围按协议及厂家支持范围校验；明显的入口参数错误（如 retry_count 小于 1）直接返回，不进入重试。

成功表示：通信已建立，厂家初始化成功，设备类型和主动轴数匹配，反馈配置读回正确，两类必要反馈的实际频率检查通过。不会自动使能或回零，也不会将设备原来的使能状态改成 false。

```text
connect(can, node_id, retry_count = 30):
    validate(can, node_id, retry_count)
    reject if already connected/connecting or duplicate device owner
    start control worker
    on worker:
        initialized = false
        for attempt in 1..retry_count:
            try:
                create vendor instance and stable send callback
                open and bind SocketCAN
                register callback
                start receive thread
                check vendor.initial(LCN_CANFD, node_id)
                verify hand type and active_axes == 6
                initialized = true
                break
            on failure:
                record initialization stage, attempt and error
                clean up this attempt's SDK/receiver/socket
                if another attempt remains: wait internal retry interval
        if not initialized:
            release remaining connection resources; return initialization error

        for attempt in 1..retry_count:
            try:
                prepare feedback strategy  // stop_monitor 建议见下文
                deadline = now + internal configuration-check timeout
                configure all six axes to 20 ms and verify readback
                configure 0x50 and 0x5A, interval multiplier = 1
                reset observation window after configuration
                verify both frame types arrive at approximately 50 Hz
                connected = true
                return success
            on failure:
                record configuration/check stage, attempt and error
                if another attempt remains: wait internal retry interval
        clean up SDK/receiver/socket and remaining connection resources
        return last configuration/check error
```

厂家说明 `initial()` 返回 `LER_COMM_RECV` 时，其后台初始化可能已经完成；本封装仍将本次连接视为失败并清理，不能因对象部分可用而报告连接成功。

初始化失败时参考 `arm_sdk_minimal` 重建厂家实例、接收线程和 socket，不在半初始化实例上再次调用 initial。初始化通过后，配置或检查失败只重试第二阶段，保留已初始化的连接，不重新开启第一阶段计数。第二阶段任一步失败（包括收不到反馈、频率不符）都算一次失败；全部配置和检查通过才算连接成功。收到 disconnect 取消请求时停止重试并清理，不等待耗尽次数。

`stop_monitor()` 用于避免厂家后台轮询与我们的 SDO 配置、主动上报频率统计相互干扰。需先用匹配版本确认停止监控不影响主动帧解码及运动接口；若有依赖，需调整监控策略并重新验证频率统计。

### 4.2 周期配置不是只写一个索引

协议要求同时配置六个轴的周期对象，串行写入并逐一检查：

```text
indices = [0x201D, 0x205D, 0x209D, 0x20DD, 0x211D, 0x215D]
subindex = 0x14
period unit = 0.1 ms

period_ticks = 200  // 200 * 0.1 ms = 20 ms
n50 = n5A = 1
for index in indices:
    write_sdo_verified(index, 0x14, period_ticks, deadline)

send to 0x500 + node_id:
    [0x00, 0x04, 0x50, n50, 0x5A, n5A]
```

每类频率为 `10000 / (period_ticks * n)` Hz。这里固定 ticks=200、n=1，使 `0x50`、`0x5A` 各自为 50 Hz。设备是否接受该配置由写应答、读回和实际频率共同检查，不把“厂家 setter 返回成功”视为整个配置成功。

SDO 优先调用 `set_sdo_drive_param(index, subindex, value)` / `get_sdo_drive_param(index, subindex, &value)`。它们的返回成功是否包含新设备应答，需要针对当前库验证；接收侧需按 index/subindex 匹配新应答，而不是只比较可能来自缓存的 getter 值。

```text
write_sdo_verified(index, sub, value, deadline):
    register pending transaction before send
    vendor.set_sdo_drive_param(index, sub, value)
    wait matching fresh write acknowledgement; abort/timeout => error
    request a fresh read through vendor.get_sdo_drive_param(...)
    wait matching fresh read response
    require returned value == value
```

同一设备一次只允许一个 SDO 事务。协议写成功应答为 `0x60`；现有示例记录当前库可能拒绝该应答，因此应先由匹配器识别，不能直接因其被厂家解码器拒绝而判定总线坏了，也不能不匹配 index 就丢弃所有 `0x60`。SDO abort、迟到应答和重试须分别处理；超时后不能把旧应答当成新事务结果。

如果厂家 getter 只返回缓存、无法发起所需的 fresh read，再在适配层补充最小的原始 SDO 请求：读为 `0x40`，周期的 16 位写为 `0x2B`，请求 ID 为 `0x600+node_id`。不要同时让两套实现争用同一事务。

第一版不调用 `save_sdo_drive_param()`，不主动写 EEPROM。中途失败可能已改变部分设备周期；错误信息应记录完成的索引，不能声称释放本地资源就回滚了设备配置。下次连接重新配置全部六轴。

### 4.3 实测反馈频率

配置后清空用于验收的统计窗口，分别收集合法 `0x50`、`0x5A` 帧的单调时钟接收时间。使用 `(N-1)/(last-first)` 计算平均接收频率，同时检查最大间隔和最近一帧年龄，避免仅凭短时突发通过检查。

每类都必须有足够样本、频率处于 50 Hz 的容差范围且无过长中断，才通过连接检查。统计窗口、最小样本数和容差尚待实测；每次第二阶段的内部期限必须覆盖六轴 SDO 和统计窗口。每次重试重新统计，不能用上次尝试的样本通过检查。

厂家 `initial()` 和某些 SDO 调用没有外部 timeout 参数，因此重试次数只限制尝试数量，不构成整个 connect 的硬性墙钟上限。内部等待必须有期限；仍需测量厂家阻塞上限。禁止为提前返回而 detach 仍引用 Hand 的线程，须保留生命周期直到厂家调用结束并完成清理。

## 5. enable()（已确认）

入口无参数，C++ 返回 `bool`。内部可以重复发送并等待新的状态反馈，但只有六轴全部确认使能才返回 `true`；未连接、发送失败、反馈无效或未全部使能返回 `false`，不返回部分状态数组。部分轴状态由 `get_state()` 提供。

```text
enable():
    require connected and fresh status; otherwise return false
    require no homing or conflicting operation; otherwise return false
    if all axes already enabled:
        return [true, true, true, true, true, true]
    on worker:
        check vendor.set_control_mode(0, LCM_POSITION); failure => return false
        check vendor.set_move_no_home(0); failure => return false
        operation_baseline = state sequence
        for attempt in 1..30:
            if cancelled by stop/disable/disconnect: return false
            baseline = state sequence
            call vendor.set_enable(0, 1)
            record vendor/send failure if any, then retry within attempt limit
            wait for fresh status after baseline within internal wait window:
                if all axes enabled:
                    return enabled[6]
            record attempt, latest status and failure reason
        if latest status is newer than operation_baseline and still fresh:
            return latest enabled[6]
        return false  // 记录反馈缺失、过期或通信错误
```

数组按固定六通道顺序返回，true 是已使能、false 是未使能，不是成功/失败标志。正常返回 `{true, true, false, true, true, false}` 表示本次重试结束时最后有效反馈中第 3、6 轴仍未使能。返回值只是快照；调用方需自行判断是否全部为 true。

每轮仅依据新反馈确认，不能以旧数组、厂家 setter 返回值或本地目标状态替代设备反馈。等待窗口建议参考现有示例使用 500 ms，收到满足条件的新反馈立即返回；此时长仍作为待实测的内部参数。窗口等待由操作状态机处理，不阻塞控制工作线程接收 stop/disable/disconnect。

未连接、反馈缺失或过期、准备阶段失败、冲突操作及取消等情况返回 `false`。使能发送失败计入内部尝试并记录原因；厂家错误和底层错误保留在日志中，不把未确认状态伪装为成功。

`LCM_POSITION` 是我们采用的厂家位置接口模式，不额外向用户开放控制模式切换。协议对底层模式的命名与 SDK 枚举不完全一致，映射交给厂家库，不直接将枚举值写进协议。

使能不设置目标位置，不自动回零。部分轴已经使能或正在运动时如何允许整手使能，需要实测确定；建议拒绝与现有运动冲突的模式切换。使能失败不得设置本地 enabled=true，也不自动失能、清告警或断开连接。

厂家支持 `set_enable(0, 1)` 整手操作和 `set_enable(id, 1)` 单轴操作，本版采用整手重试，不维护六套单轴重试计数。协议要求控制命令变化才生效；实施前需通过回调抓帧确认重复调用是否实际发包及如何重新触发，不能仅凭调用次数认定重试有效，不擅自通过先失能再使能实现重试。

## 6. disable()（已确认）

入口为 `bool disable()`，无参数。内部取消旧运动和使能等待；只有六轴都收到新的失能反馈才返回 `true`，否则返回 `false`。部分轴状态由 `get_state()` 提供。

```text
disable():
    require connection/transport exists; otherwise return false
    invalidate queued motion requests, cancel enable retries and homing wait
    block new enable/home/motion requests while disabling
    if fresh feedback already confirms all axes disabled:
        return [false, false, false, false, false, false]
    on worker, high priority:
        operation_baseline = state sequence
        for attempt in 1..30:
            if cancelled by disconnect: return false
            baseline = state sequence
            call vendor.set_enable(0, 0)
            record vendor/send error if any, then retry within attempt limit
            wait for fresh status after baseline within internal wait window:
                if all axes disabled:
                    return enabled[6]  // all false
            record attempt, latest status and failure reason
        if latest status is newer than operation_baseline and still fresh:
            return all latest enabled values are false
        return false  // 记录反馈缺失、过期或通信错误
    always release disabling-operation guard; retain connection unless disconnect requested
```

直接发送失能，不以 stop 成功为先决条件；不先调用 stop 并等待成功。反馈过期时仍允许尝试下发失能，不能先因反馈过期而拒绝发送。底层已失去发送能力时记录通信错误，不把操作报告为已确认成功。每轮等待窗口建议与 enable 一致，初步参考 500 ms，最终值待实测。

`disable()` 的 `true` 只表示六轴全部失能。未连接、最终反馈缺失或过期、被 `disconnect()` 取消等情况返回 `false`，不把未知状态当成成功。

取消旧请求必须在控制工作线程串行化：已经进入的厂家调用返回后再执行失能，失能期间拒绝新的使能或运动请求，避免交错导致刚失能又重新使能。等待期间保持接收线程运行，不阻塞断开取消。重复失能命令是否发包和生效需要验证，不通过使能再失能制造控制边沿。

失能不清除告警，不保证机械松脱，不自动重置设备回零状态。实际 `homed` 仍以设备反馈为准。

## 7. home()（已确认）

入口为 `bool home()`，无参数。内部跟踪六轴回零状态；只有六轴都完成本次回零确认才返回 `true`，否则返回 `false`。整手广播优先，后续只对未观察到进入回零的轴单独补发。

```text
home():
    require connected, fresh feedback, all axes enabled; otherwise return false
    reject if another homing operation is active
    require no ongoing position motion; otherwise return false
    on worker:
        clear per-axis observed_homing flags for this operation
        record operation generation and state sequence
        home_attempts[1..6] = 1  // 首次广播计入每轴一次
        check vendor.home_motors(0)
        register HomingOperation(deadline)
    calling thread waits for operation result

on each fresh status while HomingOperation is active:
    for each axis:
        if running_state == HOMING: observed_homing[axis] = true
        complete[axis] = observed_homing[axis]
                         and homed[axis] and zero_speed[axis]
                         and running_state != HOMING
    if all complete:
        on worker: vendor.set_control_mode(0, LCM_POSITION)
        invalidate pre-home pending targets
        return [true, true, true, true, true, true]
    every approximately 1 second:
        for each axis not observed_homing:
            if home_attempts[axis] >= 30:
                return false  // 记录该轴未进入回零运行
            check vendor.home_motors(axis)
            home_attempts[axis] += 1
    if deadline exceeded or feedback lost:
        attempt stop; return false  // 记录回零超时和停止结果
```

逻辑沿用 `arm_sdk_minimal`：首次广播后，每隔约 1 秒只补发尚未观察到回零运行的轴；已经观察到开始运行的轴不再重发，避免重启正在进行的回零。每轴最多 30 次，包含广播的一次；六轴都完成才返回六个 true。已有 `homed=1` 不能直接代表本次调用完成。

上述“先观察回零运行，再观察完成”是保守确认方式：回零很快或反馈丢失时可能漏掉运行阶段，届时可能触发不必要补发或最终失败，需要实机验证。整个操作还需要内部总超时；`arm_sdk_minimal` 当前使用 80 秒，第一版可先沿用，最终以实测调整。超时、反馈中断或达到单轴次数上限时，尝试 `stop()` 并返回 `false`，不把部分完成当成成功。

收到显式 `stop()` 时终止回零等待，执行停止；收到 `disable()` 时执行失能；收到 `disconnect()` 时终止等待并断开，仍不暗含停机。取消等待不等于设备已停下。

第一版不额外设置厂家回零电流 `set_home_current()`。它是专门的回零参数，单位为百分比，不能把普通电流上限的千分比直接传进去。回零后速度、电流是否被固件重置，也要纳入验证。

## 8. set_positions(values)（已确认）

这是第一版最薄的运动接口。公共入口只检查输入是否为六个合法位置值（每个值在 0～10000），然后按固定顺序调用厂家 SDK；不在我们这一层检查连接、使能、回零、反馈新鲜度、到位状态或设备能力，不做重试，不等待设备反馈，也不承诺目标一定执行。厂家调用或发送失败返回 `false`，调用者负责保证调用前置状态，设备是否接受和执行由后续 `get_state()` 判断。

```text
set_positions(q):
    validate q has exactly 6 values
    validate every q[i] is in [0, 10000]
    invalid input -> return false

    on the single vendor-control worker:
        for id in 1..6:
            vendor.set_target_position(id, q[id-1])
        vendor.move_motors(0)
    return immediately
```

这里不等待设备反馈，也不把发送成功表述为“已运动到位”。厂家调用返回错误或底层发送失败返回 `false`；设备未使能、未回零、设备拒绝目标或目标未执行由后续 `get_state()` 判断。输入数量和位置范围由公共层做最小检查，其他设备限制交由厂家 SDK 处理。

六个目标按 1～6 依次写入厂家缓存，再调用一次 `move_motors(0)`。本地调用返回只表示这些函数调用已经完成，不表示 CAN 帧已经到达设备、设备已接受目标、六轴已同时启动或已经到位。第一版不内置轨迹和无限周期重发；连续控制由用户循环调用。相同目标是否被厂家抑制、是否重新发送，需要抓帧及实机验证。

调用仍需在厂家要求的同一控制线程执行。SDK 可以通过工作线程串行化多个调用，但不在该线程之外并发调用厂家 setter。若用户在未连接或对象已关闭时调用，行为由生命周期约定和厂家库决定，公共接口不额外拦截。

## 9. set_position(id, value)（已确认）

这是 `set_positions()` 的单轴版本，只做输入合法性检查。调用者负责保证设备前置状态；厂家调用结果和设备执行结果不在公共接口中确认。

```text
set_position(id, q):
    validate id in [1, 6], q in [0, 10000]
    invalid input -> return false

    on the single vendor-control worker:
        vendor.set_target_position(id, q)
        vendor.move_motors(id)
    return immediately
```

直接用厂家的单轴入口，不先读取其他五轴位置再广播覆盖它们。厂家调用或发送失败返回 `false`；本地返回 `true` 仍只表示发送流程成功，不保证设备接受或执行。实际结果由后续 `get_state()` 判断。

`id` 只允许 1～6，位置只允许 0～10000；这是公共层唯一的设备相关输入检查。是否影响其他轴依赖厂家 `move_motors(id)` 的单轴语义，SDK 不额外广播或保证其他轴状态。调用仍由同一厂家控制线程串行执行，不建立队列、不重试。

## 10. set_speed(value) / get_speed()（已确认）

这是一个需要比位置指令更稳定的参数配置接口，但厂家没有速度参数的独立设备确认反馈。公共接口把“厂家缓存已一致”和“设备实际已应用”明确区分。

```text
set_speed(v):
    validate v is a legal speed input
    invalid input -> return false

    on the single vendor-control worker:
        for attempt in 1..30:
            rc1 = vendor.set_position_velocity(0, v)
            rc2 = vendor.apply_motion_params()
            read all six vendor target-speed caches:
                vendor.get_position_velocity(id, &cached[id])
            if rc1 == 0 and rc2 == 0 and all cached[id] == v:
                save requested speed
                return
            wait internal retry interval
    return false  // 记录速度配置错误

get_speed():
    on the single vendor-control worker:
        for id in 1..6:
            vendor.get_position_velocity(id, &speed[id])
        return speed[6]
```

`set_speed()` 的 30 次包含第一次尝试，不能把 connect 的 retry_count 传给用户。它调用 `apply_motion_params()`，不额外触发 `move_motors()`；后续位置运动仍由 `set_positions()` 或 `set_position()` 触发。现有程序使用 1～20000 当量/秒，正式公共范围仍需根据厂家确认；在范围未定前，只实现明确的输入类型和非负/正值检查规则。

`get_speed()` 返回六个轴的厂家目标速度缓存，不是设备读回，也不是当前实际速度。`get_state().velocities` 才使用 `0x50` 反馈表示实际运动速度。即使缓存检查通过，也只能说明厂家 SDK 参数一致，不能保证 CAN 帧已到达、设备已接受或当前运动已按该速度执行。

相同速度可能因厂家缓存而不重新产生 CAN 帧；`apply_motion_params()` 用于请求立即应用，但仍没有独立设备 ACK。重试只重试厂家调用和缓存检查，不把它描述为设备确认；底层日志可记录原始返回码和发送结果。

## 11. set_current_limit(value) / get_current_limit()（已确认）

这是与速度配置对称的稳定参数接口。它设置六轴统一的最大运行电流，单位为厂家千分比；厂家协议给出的输入范围为 0～3000。公共接口确认的是厂家 SDK 内部缓存一致，不确认设备已经应用。

```text
set_current_limit(c):
    validate c in [0, 3000]
    invalid input -> return false

    on the single vendor-control worker:
        for attempt in 1..30:
            rc1 = vendor.set_max_current(0, c)
            rc2 = vendor.apply_motion_params()
            read all six vendor current-limit caches:
                vendor.get_max_current(id, &cached[id])
            if rc1 == 0 and rc2 == 0 and all cached[id] == c:
                save requested current limit
                return
            wait internal retry interval
    return false  // 记录电流配置错误

get_current_limit():
    on the single vendor-control worker:
        for id in 1..6:
            vendor.get_max_current(id, &current[id])
        return current[6]
```

`set_current_limit()` 的 30 次包含第一次尝试，不接受用户次数参数。它不额外触发 `move_motors()`；后续位置运动使用已经配置的参数。不能默认把电流设置为协议最大值，也不能把千分比解释成 mA、N 或精确力矩。

`get_current_limit()` 返回六个轴的厂家目标电流上限缓存，不是设备读回，也不是当前实际电流；`get_state().currents` 才是运行反馈。`apply_motion_params()` 可能同时应用已经修改的速度和电流，两个配置接口必须在同一控制线程串行调用。

相同电流值可能因厂家缓存而不重新产生 CAN 帧；`apply_motion_params()` 只是请求立即应用，仍没有独立设备 ACK。重试只重试厂家调用和缓存检查，不把它描述为设备确认；底层日志可记录原始返回码和发送结果。

## 12. set_torque_reached_mode(mode) / get_torque_reached_mode()（已确认）

不定义额外枚举，公共模式直接使用整数：`1` 表示安全电流开启、力矩到位停止，也是默认模式；`0` 表示安全电流关闭、力矩到位保持。

```text
set_torque_reached_mode(mode):
    validate mode in {0, 1}
    invalid input -> return false

    on the single vendor-control worker:
        for attempt in 1..30:
            rc_set = vendor.set_safe_current_enable(mode)
            cached_mode = 0
            rc_get = vendor.get_safe_current_enable(&cached_mode)
            if rc_set == 0 and rc_get == 0 and cached_mode == mode:
                save requested mode
                return
            wait internal retry interval
    return false  // 记录力矩模式配置错误

get_torque_reached_mode():
    on the single vendor-control worker:
        vendor.get_safe_current_enable(&mode)
        return mode
```

行为定义保持不变：Stop 为力矩到位后停止并限制输出电流；Hold 为持续保持设定电流，阻力下降后继续向目标位置运动。Hold 不是位置锁定，也不是精确力矩控制接口。

当前 2026-09-21 头文件直接提供 `set_safe_current_enable()` / `get_safe_current_enable()`，本方案按上述 0/1 语义封装，不调用旧文档中的 `set_torque_control_mode()`。读取值表示厂家报告的配置状态，不表示当前某个轴是否力矩到位；`get_state().torque_reached` 才是运行状态。

设置成功表示厂家 setter/getter 返回成功且两者一致，不表示设备已经通过独立 ACK 或已经应用到当前运动。相同值可能被厂家缓存，30 次重试不能等同于设备确认；底层日志可以记录原始返回码和发送结果。

## 13. get_state()（已确认）

`get_state()` 返回一个本地状态快照，采用 `feedback_valid` 字段表示缓存是否足够新。它不发送 CAN 查询、不重试、不等待下一帧；反馈过期时仍返回最后缓存，但调用者不得把过期缓存当成当前设备状态。

```text
receive 0x50:
    validate six-axis payload and decode through vendor
    for id in 1..6:
        vendor.get_now_position(id, &positions[id-1])
        vendor.get_now_position_velocity(id, &velocities[id-1])
        vendor.get_now_current(id, &currents[id-1])
    publish these arrays and their receive timestamp/sequence

receive 0x5A:
    validate and decode through vendor
    for id in 1..6:
        s = first status byte for this axis
        enabled = bit0; position_reached = bit1; zero_speed = bit2
        torque_reached = bit3; homed = bit4; running_state = bits5..7
        fault_code = second status byte & 0x0F
    publish status fields and their receive timestamp/sequence

get_state():
    lock local snapshot; copy; unlock
    feedback_valid = connected and no receive failure
                     and both required frame groups received
                     and both ages below configured threshold
    return snapshot with feedback_valid
```

公共字段为 `positions[6]`、`velocities[6]`、`currents[6]`、`enabled[6]`、`homed[6]`、`torque_reached[6]`、`fault_codes[6]` 和 `feedback_valid`。零速、运行状态、位置到位、各类帧时间戳和序号先作为内部判断字段，不额外扩展公共接口。

建议 `fault_codes` 明确定义为 `0x5A` 原始简易报警码：0 无报警，1～8 对应协议类别，15 表示其他。它不是 `0xDB` 的完整 12 位故障码，也不是厂家 getter 的 `LAM_*` 枚举。若改用 `get_now_alarm()`，必须统一转换并更新字段定义，不能混用两套数值。

厂家还提供 `get_enable()`、`get_position_reached()`、`get_torque_reached()`、`get_now_status()`；本方案为确认使能、回零和停止，直接使用当前收到的 `0x5A` 状态位，避免将本地控制缓存当成新设备反馈。

一份快照中的位置与状态可能来自不同时间的两帧；本地复制一致不等于设备同时采样。接收时间用 `steady_clock`，有效性按调用时重新计算。没有反馈时缓存数值无效，不允许仅因数组默认值为零就认定设备正常。反馈周期固定为 20 ms，过期阈值先作为内部配置，建议初始取 100 ms，实测后再调整。

## 14. stop()（已确认）

入口为 `bool stop()`，无参数。只有六轴都通过新的状态反馈确认零速且不再运行或回零才返回 `true`，否则返回 `false`。部分轴状态由 `get_state()` 提供。

```text
stop():
    require transport available; otherwise return false
    invalidate pending position requests; cancel active homing wait
    block new ordinary motion while stopping
    if fresh state already confirms all axes stopped:
        return [true, true, true, true, true, true]
    on the single vendor-control worker:
        operation_baseline = state sequence
        for attempt in 1..30:
            baseline = state sequence
            vendor.stop_motors(0)
            wait for a fresh 0x5A state after baseline:
                stopped[id] = zero_speed[id]
                              and running_state[id] not in {RUNNING, HOMING}
            if all stopped:
                return stopped[6]
        if latest status is newer than operation_baseline and still fresh:
            return all stopped
        return false  // 记录反馈缺失、过期或停止未确认
```

已停止且有新鲜反馈时幂等成功。不能只因为位置到位、力矩到位或某一帧速度为零，就断言停止命令已经生效；每轴还要确认不在正常运行或回零状态。状态确认是可观测结果，不是带事务编号的命令 ACK。重试只重复厂家停止调用和状态观察，不能保证设备一定执行。

反馈过期时仍允许尝试下发停止；没有有效反馈或确认失败时返回 `false`，不能把未知状态伪装成成功。它不主动失能，不关闭连接，也不自动清除故障。停止期间拒绝新的普通运动请求，已进入的厂家调用返回后再串行执行停止。

协议有急停命令 7 和撤销急停命令 8，而 `stop_motors()` 的公开注释未明确其映射及恢复流程。需抓取厂家实际输出，验证下一次 `move_motors()` 是否自动处理恢复。未经验证，不在公共 SDK 中猜测性补发撤销急停。停止后的恢复步骤只能作用于新目标，不能执行停止前滞留的目标。

## 15. disconnect()（已确认）

入口无参数，调用 `hand.disconnect()` 即可。职责仅为释放本地 SDK、线程和通信资源，不等待设备反馈，不调用 stop_motors 或 set_enable(0, 0)。不使用 connect 的 retry_count，也不对整个清理流程套 30 次重试。

已经完全断开时直接成功；重复或并发的断开请求共用同一次清理，避免重复销毁资源。关闭期间不接受新的普通控制请求。CAN 已断线也应继续执行本地清理，不以反馈新鲜或设备应答作为清理前提。

```text
disconnect():
    if fully closed: return success
    if cleanup already running: wait for that cleanup result
    mark closing; reject new ordinary requests
    cancel queued requests, connect retries and outstanding operation waits
    wait for any already-entered vendor call to return
    on control worker:
        vendor.close() while callback and transport still exist
    signal receive loop to exit and join it
    on control worker:
        destroy vendor instance while callback/transport objects still exist
        close socket; release device ownership and callback resources
        exit worker
    owning/calling thread joins worker
    clear connected/feedback-valid state
    return cleanup result
```

厂家示例按 SDK close 再通信 disconnect 清理；实际实现需确认 `close()` 与接收解码并发的约束。若 close 会释放解码依赖，应先停止厂家后台活动并用接收侧屏障停止新的解码，再 close，不能持有一个导致内部等待应答无法推进的锁。

回调和 socket 对象的寿命必须覆盖厂家线程关闭；不得销毁对象后留存回调，不 detach 线程，不让工作线程 join 自身。部分初始化失败也执行相应清理。析构走不抛异常的资源清理路径。

已进入厂家函数的调用不能强制中断；须等它返回后完成清理，因此不承诺立即返回。不能仅置 connected=false 就返回成功，必须实际完成资源回收。清理某一步报错时仍尝试释放其他资源，最后汇总返回错误；不盲目重复关闭已经释放的文件描述符。

断开不会自动发送停止或失能。需要停机时由用户显式 `stop()`、`disable()` 后再断开；断开期间取消 home 等待不意味着设备终止回零，设备可能继续执行最后收到的命令。再次 connect 时重建实例并重新初始化，不复用失效反馈或未完成的旧目标。

## 16. 返回结果、重试和超时

伪代码中的 `check` 统一转换厂家非零返回码和底层发送错误。建议错误包含操作名、类别、厂家码或 errno；需要时带“命令可能已发送”“停止是否确认”等上下文。

enable、disable、stop、home 统一返回 `bool`：只有接口要求的设备状态全部确认才返回 `true`，否则返回 `false`；部分状态通过 `get_state()` 读取。第一版不使用 C++ 异常，错误原因通过日志和状态字段诊断。

| 类别 | 示例 |
| --- | --- |
| 参数/状态错误 | InvalidArgument、NotConnected、NotEnabled、NotHomed、Busy |
| 等待/通信错误 | Timeout、CommunicationError、FeedbackStale |
| 功能/操作错误 | Unsupported、Cancelled、VendorError |

告警随状态返回，不自动清除、恢复或增加故障处理线程。执行某操作时厂商明确返回错误，仍应如实返回，不能将“暂不处理告警”解释成忽略所有失败。

connect 已确定采用分阶段有限重试，两个阶段各最多 retry_count 次，默认 30 次包含首次尝试。该参数只用于连接内部；enable、disable 每次调用各自固定最多尝试 30 次，不接受用户次数参数。回零等有运动副作用的操作建议默认只触发一次；停止是否重试按后续逐接口讨论确定。协议控制值“有变化才生效”，重复发送同一个值不等于重新执行。

停止/失能确认、回零等待及连接内每次配置检查需有相应期限，connect 的等待期限不作为公共参数。调用方等待超时后，未执行的请求必须取消；已进入厂家调用的请求需要保留对象生命周期并报告结果不确定，不能在用户以为取消后悄悄执行旧队列。

## 17. 建议实现顺序和验收点

1. 资源生命周期与通信：模拟回调和虚拟 CAN 验证多实例路由、过滤、FD 补齐、失败清理；验证重复断开、断线清理、连接重试中断开、等待当前厂家调用后清理及重新连接，不额外发送停止/失能命令。
2. 接收与状态：注入厂家能解析的帧，验证单位、状态位、简易报警码、过期与缺帧。
3. connect：模拟初始化失败和 SDO 应答，验证默认每阶段最多 30 次、初始化失败重建、第二阶段失败不重置第一阶段、六轴 20 ms 写入、abort/超时、读回不符、分别验收两类 50 Hz 帧及耗尽次数后的清理。
4. 参数和目标下发：厂家无硬件模式或模拟适配层验证顺序、参数校验、单轴不影响其他轴、部分发送失败。
5. enable/home/stop/disable：模拟状态变化与操作中断，验证等待新反馈和控制线程不被 home 阻塞；确认只有目标状态全部满足才返回 `true`，缺帧、过期、取消和发送失败返回 `false`，并检查实际发包与控制边沿行为。
6. 显式运行实机验收：回零完成判断、停止恢复、两种力矩模式、参数生效和实际频率。

仅验证回调、缓存或编译不能替代实机行为确认。测试程序不要在构建或普通单元测试中自动使能、回零。

审阅时优先确认：

- `get_state()` 是否采用本文建议的 `feedback_valid` 字段。
- 参数 setter 是否按本文建议立即调用 `apply_motion_params()`。
- 是否接受首次运动前显式设置速度和电流，不选隐含默认值。
- 是否接受有未完成运动下发时返回 Busy，不建立待执行目标长队列。
- 连接内部单次等待期限、重试间隔、统计窗口和频率容差待实测确定；入口三个参数、默认每阶段 30 次、固定 50 Hz 已确认。

## 18. 本地参考文件

- [接口范围总结](v1_api_summary.md)
- [厂家头文件](../../arm_sdk_minimal/sdk/aarch64/include/LHandProLib/LHandProLib.hpp)
- [现有 SocketCAN 与手控制实现](../../arm_sdk_minimal/hand.hpp)
- [现有控制工作线程](../../arm_sdk_minimal/dual_runtime.hpp)
- [现有示例说明及限制](../../arm_sdk_minimal/README.md)
- [CAN FD 协议](<../../DH116S手册整理/14_DH116(S) 系列灵巧手CANFD通讯协议.md>)
- [上位机手册](<../../DH116S手册整理/03_LHandPro 上位机软件使用说明书.md>)
- 厂家完整包：工作区 `DH116S手册整理/附件/LHandProLib-API-Linux-{架构}-20260921/`，其中 `share/LHandProLib/examples/CANFD_cpp/main.cpp` 为回调和初始化参考。

以上兄弟目录链接用于当前工作区审阅；独立克隆 `dh116s_sdk` 时，应另行准备厂家包及相关手册。
