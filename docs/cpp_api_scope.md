# C++ 核心 SDK 接口范围草案

## 1. 分层边界

```text
用户程序 / 遥操作 / 算法
            |
        C++ Core SDK
            |
    Vendor SDK Adapter
            |
       CAN/CAN-FD 通信
```

`C++ Core SDK` 是稳定的公共接口。`Vendor SDK Adapter` 负责调用厂家 `LHandProLib`、转换厂家返回码和维护异步反馈缓存。厂家头文件、厂家类型和原始错误码不进入公共头文件。

Python 通过绑定 C++ Core SDK 获得与 C++ 一致的单位、状态和错误语义。ROS 2 是独立适配层，不能成为 C++ 核心的依赖。

## 2. 第一阶段 C++ 用户接口

### 2.1 连接生命周期

```cpp
class Hand {
public:
    static Result<Hand> connect(const ConnectionOptions& options);

    Result<void> disconnect();
    bool is_connected() const;
};

struct ConnectionOptions {
    std::string interface_name;   // 例如 can0
    uint8_t device_id;
    std::chrono::milliseconds timeout{2000};
};
```

要求：支持多个 `Hand` 实例；连接、断开和析构都必须有明确的资源释放行为；连接成功只代表通信初始化完成，不代表设备已经使能或回零。

### 2.2 设备和通道信息

```cpp
Result<DeviceInfo> get_info() const;
Result<std::vector<JointInfo>> get_joint_info() const;
Result<Capabilities> get_capabilities() const;
```

`DeviceInfo` 至少包含型号、序列号（如果可读）、固件版本、左右手和通道数。`JointInfo` 至少包含通道 ID、名称、顺序、单位、最小值、最大值、方向和耦合关系。

### 2.3 使能和停止

```cpp
Result<void> enable();
Result<void> disable();
Result<void> stop(StopMode mode = StopMode::HoldPosition);

enum class StopMode {
    HoldPosition,
    CancelMotion,
    Disable
};
```

调用返回成功表示命令已发送并满足 SDK 规定的确认条件；不能把“厂家函数返回 0”直接当作电机已经停止。

### 2.4 位置控制

```cpp
Result<void> set_positions(std::span<const double> values);
Result<void> set_position(JointId id, double value);
```

第一阶段只提供位置模式。输入长度、通道顺序、范围和单位必须由 `JointInfo` 明确。范围校验在发送前完成。整手控制用于连续更新，单通道控制用于调试和简单应用。

### 2.5 反馈状态

```cpp
Result<HandState> get_state() const;

struct HandState {
    Timestamp stamp;
    std::vector<JointState> joints;
    FaultState fault;
    bool enabled;
    bool homed;
    bool feedback_fresh;
};
```

位置是第一阶段必选反馈；速度、电流、温度按硬件能力提供，不可用字段必须能明确表达缺失。`feedback_fresh == false` 时，调用方不能把缓存值当作当前设备状态。

### 2.6 回零和故障

```cpp
Result<void> home();
Result<void> clear_faults();
```

`home()` 是有持续时间和完成状态的操作，后续可增加异步句柄；第一版同步接口必须有超时和取消语义。无法清除的故障返回 `Unsupported` 或明确的设备故障。

## 3. 错误语义

公共层定义稳定错误类别：

```text
InvalidArgument, NotConnected, NotEnabled, Busy,
Timeout, CommunicationError, FeedbackStale,
DeviceFault, Unsupported, Cancelled, VendorError
```

错误对象保留操作名、是否可重试以及厂家原始错误码，便于日志和现场排查。公共程序不需要依赖厂家数字错误码才能完成正常处理。

## 4. 线程和调用约定

- 每个 `Hand` 实例独占一个厂家 SDK 实例。
- 接收线程负责收包、解码和更新状态缓存。
- 控制命令通过 SDK 内部串行化，避免多个线程同时调用厂家 setter。
- `get_state()` 返回一个一致性快照。
- `stop()`、`disable()` 和关闭流程拥有高于普通运动更新的优先级。
- SDK 内部必须区分命令发送成功、设备反馈确认和动作完成。

## 5. ROS 2 初步映射

ROS 2 只依赖 C++ Core SDK：

| 类型 | 初步接口 | 说明 |
|---|---|---|
| Service | `connect`、`disconnect`、`enable`、`disable`、`stop`、`clear_faults` | 短时命令 |
| Topic | `state`、`joint_states`、`faults` | 周期反馈 |
| Action | `home`、`move_to_positions` | 有进度、完成和取消的操作 |

高频位置更新不使用 service；优先使用 topic 或后续的实时控制接口。ROS 消息中的单位、通道顺序和错误码必须与 C++ SDK 文档一致。

## 6. 暂不纳入第一阶段

- 力控和阻抗控制
- 轨迹规划器
- 手势库和复杂示教流程
- 厂家私有参数的完整透传
- 实时安全控制器

这些能力需要先确认厂家 SDK 的稳定性、反馈频率、实时性和硬件支持，再单独扩展能力接口。
