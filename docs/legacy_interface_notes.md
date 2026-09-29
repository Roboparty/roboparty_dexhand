# 历史接口讨论记录（非当前使用文档）

当前接口更新：`connect(can_name, node_id)` 不再公开重试参数；连接两阶段各固定最多尝试 30 次（含首次）。下文 `retry_count` 仅保留为历史记录。

本文保留实现前的 README 原文，其中数组返回值、异常和参数 getter 等描述已被后续第一版 bool 接口取代。当前用法见 [README](../README.md)，公共接口见 [v1_api_summary.md](v1_api_summary.md)。下文旧相对链接按原文保留。

DH116S 灵巧手的二次开发目录。

## 目录

- `include/`：面向用户的 C++ 头文件
- `src/`：C++ 核心实现和厂家 SDK 适配层
- `python/`：Python 绑定与 Python API
- `ros2/`：ROS 2 适配包
- `tests/`：无硬件测试、协议测试和集成测试
- `docs/`：架构、接口和行为约定

## 当前范围

第一阶段提供连接、使能、回零、行程位置控制、速度与电流设置、力矩到位模式、状态读取和停止。告警随状态返回，暂不提供设备信息查询、通道信息查询和故障清除接口。

当前接口讨论总结见 [`docs/v1_api_summary.md`](docs/v1_api_summary.md)。此前的 [`docs/cpp_api_scope.md`](docs/cpp_api_scope.md) 保留为早期草案，范围冲突时以当前总结为准。

C++ 第一版逐接口实现逻辑、厂家函数映射及伪代码见 [`docs/cpp_v1_implementation_plan.md`](docs/cpp_v1_implementation_plan.md)，当前处于审阅阶段，尚未实现。

## 已确认的接口约定

以下为已讨论确认的设计，不表示接口已经实现。其余接口继续逐项讨论。

### connect(can, node_id, retry_count = 30)

- 只接收 CAN 接口、节点 ID 和重试次数；默认每阶段最多尝试 30 次，包含第一次。
- 第一阶段建立通信并调用厂家初始化；失败清理本次资源，重建实例后重试。
- 第二阶段配置六轴反馈周期为 20 ms，配置并分别检查 `0x50`、`0x5A` 两类反馈各自约为 50 Hz；失败保留连接，重试本阶段。
- 两个阶段分别计数，全部通过才连接成功；任一阶段次数耗尽则清理并返回失败。
- 不自动使能或回零。主机 CAN FD 接口需事先配置并启用。

### disconnect()

- 无参数，取消待执行请求和操作等待，协调当前厂家调用结束，关闭厂家 SDK、回收线程及 socket。
- 已完全断开时重复调用直接成功；不使用连接的重试次数，不等待设备应答。
- 不自动停止或失能。断开后设备可能继续执行最后收到的命令；需要停机时先显式调用 `stop()`、`disable()`。

### enable()

```cpp
std::array<bool, 6> enable();
```

- 无参数，内部固定最多尝试 30 次，包含第一次，不受 connect 的 retry_count 影响。
- 整手调用厂家 `set_enable(0, 1)`，根据新的状态反馈逐轴确认；六轴全部使能即提前返回。
- 若调用前的新鲜反馈已确认六轴使能，直接返回六个 true。
- 次数耗尽但有本次操作期间收到、且仍然有效的状态反馈时，返回最后一份六轴使能状态；允许部分为 false。
- true 表示该轴已使能，false 表示该轴未使能。返回数组不等于六轴全部使能，调用方需要检查数组内容。
- 未连接、反馈缺失或过期、操作被取消等无法正常给出状态的情况抛异常，不返回虚假的全 false；具体异常类型待实现时统一。
- 不自动回零、不下发位置目标。未全部使能时不自动失能、清除告警或断开连接。

数组顺序固定为：拇指侧摆、拇指屈伸、食指屈伸、中指屈伸、无名指屈伸、小指屈伸。例如 `{true, true, false, true, true, false}` 表示最后有效反馈中第 3、6 轴未使能。

返回值是反馈快照，不保证设备之后一直保持该状态；后续变化通过 `get_state()` 获取。协议要求控制命令变化才生效，重复调用厂家 set_enable 的实际发包及重复触发行为仍需验证，不通过自动失能再使能来制造重试。

### disable()

```cpp
std::array<bool, 6> disable();
```

- 无参数，内部固定最多尝试 30 次，包含第一次，不受 connect 的 retry_count 影响。
- 优先取消待执行运动、使能重试和回零等待，避免失能后旧请求再次驱动设备。
- 整手调用厂家 `set_enable(0, 0)`，等待新的反馈逐轴确认；六轴全部失能即提前返回。
- 返回的仍是六轴使能状态，顺序与 enable 相同：false 为已失能，true 为仍使能。全部失能返回六个 false。
- 调用前的新鲜反馈已经确认全部失能时直接返回；次数耗尽且有本次操作期间的新反馈、仍未过期时，返回最后一份数组，允许部分 true。
- 反馈过期时仍尝试下发失能，不以 stop 成功为前提。最终反馈缺失或过期、未连接、操作被断开取消等情况下抛异常，不用全 false 表示无法确认。
- 保留连接，不自动清告警，不强制修改回零状态；实际 homed 以设备反馈为准。失能不保证手指张开或机械松脱。

例如 `{false, false, true, false, false, true}` 表示最后有效反馈中第 3、6 轴仍使能。重复失能命令的实际发包及触发行为同样需验证，不通过重新使能来制造控制边沿。

### home()

```cpp
std::array<bool, 6> home();
```

- 要求已经连接、六轴已使能，且当前没有其他运动；不自动调用 `enable()`。
- 首次调用厂家 `home_motors(0)`，整手触发六轴回零。
- 后续只要仍有轴未观察到进入“回零运行”，就约每秒调用 `home_motors(0)` 整手补发；不进行单轴补发，可能重新触发其他轴。六轴都观察到启动后只等待完成。
- 整手最多尝试 30 次，包含首次触发；整个回零过程还有内部总超时。具体等待间隔和总超时时间以实现配置为准。
- 六轴都必须先观察到本次回零运行，再观察到已回零、退出回零运行且零速，才返回六个 `true`。
- 回零完成后恢复厂家位置模式；不把发送位置 `0` 当作回零。
- 超时、反馈中断或被 `stop()`、`disable()`、`disconnect()` 取消时抛异常，不返回部分完成数组冒充成功。取消等待不代表设备已经停止。
- 不额外设置厂家回零电流；不自动清除告警。

### set_positions(values)

```cpp
void set_positions(const std::array<int, 6>& values);
```

- 按固定六轴顺序直接调用厂家 `set_target_position(id, value)`，然后调用一次 `move_motors(0)`。
- 只检查输入数量和范围：必须是 6 个值，且每个值在 `0～10000`；不检查设备状态和厂家返回结果。
- 输入合法后直接调用厂家接口并返回，不保证厂家调用成功、CAN 帧已到达设备、设备接受目标或机械运动已经发生；目标是否执行由后续 `get_state()` 反馈判断。
- 调用顺序由 SDK 保证，厂家控制接口仍在同一控制线程执行；其他错误交由厂家 SDK 处理。

### set_position(id, value)

```cpp
void set_position(int id, int value);
```

- 只检查 `id` 是否在 `1～6`、`value` 是否在 `0～10000`。
- 输入合法后直接调用厂家 `set_target_position(id, value)` 和 `move_motors(id)`。
- 不检查连接、使能、回零、其他轴状态、厂家返回值或设备反馈，不重试、不等待到位。
- 返回只表示本地调用流程结束，不保证设备接受或执行目标；实际结果通过 `get_state()` 查看。

### set_speed(value) / get_speed()

```cpp
void set_speed(int value);
std::array<int, 6> get_speed();
```

- `set_speed()` 给六个轴统一设置位置运动速度，单位为厂家行程当量/秒。
- 输入合法后最多尝试 30 次；每次调用厂家 `set_position_velocity(0, value)`，再调用 `apply_motion_params()`。
- 通过六次 `get_position_velocity(id, &value)` 检查厂家 SDK 内部保存的目标速度；六轴一致后返回。
- 读取到的是厂家缓存的目标速度，不是设备对速度参数的总线确认，也不是当前实际速度。
- 30 次后仍不能确认厂家缓存一致时抛异常；不保证设备已经应用速度。
- `get_state()` 中的 `velocities[6]` 才是设备运动时的实际速度反馈。

### set_current_limit(value) / get_current_limit()

```cpp
void set_current_limit(int value);
std::array<int, 6> get_current_limit();
```

- 电流单位为厂家千分比，输入范围为 `0～3000`。
- `set_current_limit()` 最多尝试 30 次；每次调用厂家 `set_max_current(0, value)`，再调用 `apply_motion_params()`。
- 通过六次 `get_max_current(id, &value)` 检查厂家 SDK 内部保存的六轴上限是否一致，一致后返回。
- `get_current_limit()` 读取的是厂家目标电流上限缓存，不是设备实际电流，也不是设备 ACK。
- 30 次后仍不能确认缓存一致时抛异常；即使设置成功，也不保证设备已经应用该上限。
- `get_state().currents` 表示设备运行时实际电流，不能与该配置值混淆。

### set_torque_reached_mode(mode) / get_torque_reached_mode()

```cpp
void set_torque_reached_mode(int mode);
int get_torque_reached_mode();
```

- `mode = 1`：安全电流开启，力矩到位停止；这是默认模式。
- `mode = 0`：安全电流关闭，力矩到位保持。
- 设置最多尝试 30 次：调用厂家 `set_safe_current_enable(mode)`，再读取厂家状态确认值。
- 读取返回 `0` 或 `1`，表示厂家报告的配置状态；不是当前力矩到位状态，也不是设备运动确认。
- 设置成功不代表当前已经触发力矩到位；`get_state().torque_reached` 才表示各轴当前是否触发。

### get_state()

```cpp
struct HandState {
    std::array<int, 6> positions;
    std::array<int, 6> velocities;
    std::array<int, 6> currents;
    std::array<bool, 6> enabled;
    std::array<bool, 6> homed;
    std::array<bool, 6> torque_reached;
    std::array<int, 6> fault_codes;
    bool feedback_valid;
};

HandState get_state();
```

- 只读取接收线程维护的最新反馈缓存，不发送 CAN 查询、不重试、不等待下一帧。
- `0x50` 更新位置、实际速度和实际电流；`0x5A` 更新使能、回零、力矩到位和简易告警码。
- `feedback_valid` 表示两类必要反馈都曾正常收到，且最近更新时间没有超过内部过期阈值；它不表示无告警、已到位或运动成功。
- 反馈过期时仍返回最后缓存，方便查看最后状态，但调用者不能把这些值当成当前设备状态。
- `fault_codes` 为 `0x5A` 状态2低四位：0 无报警，1～8 为协议定义的简易报警类别，15 为其他报警。
- 实际运动速度和电流分别是 `velocities`、`currents`，不是 `get_speed()` 和 `get_current_limit()` 返回的配置缓存。

### stop()

```cpp
std::array<bool, 6> stop();
```

- 取消本地待执行的位置命令和回零等待，调用厂家 `stop_motors(0)` 停止六轴。
- 固定最多尝试 30 次，等待新的 `0x5A` 状态反馈；各轴确认零速且不再处于运行或回零状态后，对应返回值为 `true`。
- 六轴全部确认停止时提前返回；次数耗尽但仍有有效反馈时返回最后一次六轴停止状态。
- 没有有效反馈、操作被断开取消或无法确认停止时抛异常，不返回全 `false` 表示未知状态。
- 不自动失能、清除告警或断开连接，不保证保持当前位置，也不自动发送急停撤销或恢复命令。
