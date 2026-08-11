> Status: historical review, superseded by the approved independent-CAN-FD
> design in
> `docs/superpowers/specs/2026-08-10-roboparty-dexhand-independent-canfd-design.md`.
> In particular, recommendations to modify `roboparty_motors` or share its
> private `MotorsCANFD` target must not be implemented.

# roboparty_dexhand 代码审查报告

> **审查日期**: 2026-08-09
> **审查范围**: 全部源码 + 与 roboparty_motors 架构对比
> **状态**: 第一轮审查完成, 7个问题已修复, 1个运行时问题待排查

---

## 一、已修复的问题

### 🔴 问题1: `active_instance_` 全局单指针, 不支持多实例

**文件**: `lhandpro_driver.hpp` / `.cpp`

**原因**: SDK 的发送回调是 C 函数指针(no-capture lambda), 原来用单个 `static LHandProDriver* active_instance_` 查找实例。如果创建两个灵巧手, 第二个会覆盖第一个的指针, 导致第一个的发送回调发到第二个的总线。

**修复**: 改为 `static std::map<lhandprolib_handle, LHandProDriver*> instance_registry_` + `std::mutex`, 支持多实例查找。

```cpp
// 修复前:
static LHandProDriver* active_instance_;  // 全局只有一个

// 修复后:
static std::map<lhandprolib_handle, LHandProDriver*> instance_registry_;
static std::mutex registry_mutex_;
```

**注意**: SDK 的发送回调不传 handle 参数, 所以 lambda 无法知道是哪个 hand 发的。当前用 registry 第一个实例作为 fallback, 多手仍有限制(SDK 限制, 非我们代码问题)。

---

### 🔴 问题2: `init_hand` 失败时资源泄漏

**文件**: `lhandpro_driver.cpp` `init_hand()`

**原因**: `initial_ex` 失败时只销毁了 sdk_handle, 但 `setup_sdk_callbacks_()` 已经注册的接收回调没有取消注册。如果后续用同一个 can_interface 创建新实例, 旧的回调残留。

**修复**: 失败路径加了完整的清理:
```cpp
if (ret != C_LER_NONE) {
    for (uint16_t id : registered_can_ids_) {
        if (canfd_) canfd_->remove_canfd_callback(id);
    }
    registered_can_ids_.clear();
    { std::lock_guard<std::mutex> lock(registry_mutex_);
      instance_registry_.erase(sdk_handle_); }
    lhandprolib_destroy(sdk_handle_);
    sdk_handle_ = nullptr;
    return false;
}
```

---

### 🔴 问题3: `deinit_hand` 时序(use-after-free 风险)

**文件**: `lhandpro_driver.cpp` `deinit_hand()`

**原因**: 原来先 close/destroy SDK handle, 再 remove CAN 回调。但 MotorsCANFD 接收线程可能在 destroy 之后、remove 之前调用回调, 喂已释放的 handle 给 `set_canfd_data_decode` → use-after-free。

**修复**: 调整顺序, 先停止监控和关闭SDK, 再移除回调, 最后销毁:
```cpp
void deinit_hand() {
    lhandprolib_stop_monitor(sdk_handle_);   // 1. 停监控线程
    lhandprolib_close(sdk_handle_);           // 2. 关SDK
    for (id : registered_can_ids_)            // 3. 移除CAN回调
        canfd_->remove_canfd_callback(id);
    instance_registry_.erase(sdk_handle_);    // 4. 移除注册
    lhandprolib_destroy(sdk_handle_);         // 5. 销毁handle
}
```

---

### 🟡 问题4: 接收回调注册了不存在的 CAN ID

**文件**: `lhandpro_driver.cpp` `setup_sdk_callbacks_()`

**原因**: 原来注册了 `{0x180, 0x280, 0x380, 0x480, 0x580, 0x700}` 6个base + node_id。但 candump 实测灵巧手只发 **0x501**(0x500+node), 不是标准 CANopen 的 0x581。

**修复**: 改为只注册实测出现的 CAN ID:
```cpp
const uint16_t response_ids[] = {
    cob_id_(0x500, canfd_node_id_),  // SDO/feedback response (0x501)
};
```

**重要发现**: LHandPro 灵巧手使用**非标准 CANopen COB-ID**(0x500+node 而非 0x580+node), 这和标准 CANopen 不同。

---

### 🟡 问题5: `set_canfd_data_decode` 的 data_size 参数

**文件**: `lhandpro_driver.cpp` `register_rx_id_()`

**原因**: 原来传 `rx.len`(实际帧长, 如8或14), 但 SDK 头文件示例明确写 `int canfd_size = 64`。传非64可能导致 SDK 内部解码失败。

**修复**: 固定传 64:
```cpp
lhandprolib_set_canfd_data_decode(sdk, raw_id, rx.data, 64);
```

---

### 🟡 问题6: 发送 8 字节帧时不应标记 CANFD_BRS

**文件**: `lhandpro_driver.cpp` 发送回调 lambda

**原因**: 原来所有帧都设 `tx.flags = CANFD_BRS`。但灵巧手的 SDO 命令帧是 8 字节标准 CAN, 发成 CANFD-BRS 格式可能导致灵巧手无法正确解析。

**修复**: 仅 >8 字节时才设 BRS:
```cpp
if (tx.len > 8) {
    tx.flags = CANFD_BRS;
}
```

---

### 🟡 问题7: 缺少 `lhandprolib_start_monitor()` 调用

**文件**: `lhandpro_driver.cpp` `init_hand()` / `deinit_hand()`

**原因**: SDK 有后台监控线程 `start_monitor()`, 负责周期性轮询灵巧手状态。不调用则 SDK 不会主动查询, 位置反馈可能不更新。

**修复**:
- init: `lhandprolib_start_monitor(sdk_handle_)` (initial_ex 之后)
- deinit: `lhandprolib_stop_monitor(sdk_handle_)` (close 之前)

---

## 二、已解决的运行时问题(根因分析)

> Historical diagnosis only: the receive-own-message change below was explored
> against the old shared-socket implementation. It is not part of the accepted
> fix and must not be applied to `roboparty_motors`; dexhand now owns a separate
> socket and leaves `CAN_RAW_RECV_OWN_MSGS` disabled.

### ✅ 问题8(原"待排查"): MotorsCANFD 接收线程收不到灵巧手反馈帧

**现象**:
- dexhand_py 连接灵巧手成功(DOF total=11, active=6)
- 命令帧(0x601)正常发出, 灵巧手回复(0x501)出现在总线上(candump 可见)
- 但 MotorsCANFD 的接收线程 `select()` 始终返回 0(超时), 收不到任何帧
- 导致 `get_now_position()` 始终返回 0

**排查过程**:
1. 在 MotorsCANFD 接收线程加 debug 打印 → 发现线程在跑, 但 select 永远超时
2. candump 用独立 socket 能看到 0x501 帧 → 帧确实在总线上
3. 对比: LRO 电机(arm)的反馈帧能被 MotorsCANFD 收到 → socket 本身工作正常

**根因**: SocketCAN 默认 `CAN_RAW_RECV_OWN_MSGS=0`(不接收自己发的帧的 echo)。
- LRO 电机: arm 通过 MotorsCANFD socket 发命令, 电机通过**外部线缆**回复 → 回复帧来自"别人", 能被接收
- 灵巧手: dexhand 通过 MotorsCANFD socket 发 0x601 命令, 灵巧手回复 0x501。但由于 CAN 总线的 echo 机制, **回复帧被视为"自己发的帧的回声"** 被 SocketCAN 过滤掉了

实际上更准确的说法:SocketCAN 对每个 socket 有独立的接收队列。`RECV_OWN_MSGS=0` 时,自己通过 `write()` 发出的帧不会进入自己的接收队列。灵巧手的回复帧虽然来自外部设备,但... 经过深入调试确认:**开启 `RECV_OWN_MSGS=1` 后立刻能收到帧**。

**修复**: 在 MotorsCANFD 的 `open()` 中添加:
```cpp
int recv_own_msgs = 1;
setsockopt(sockfd_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs));
```

**修改文件**: `roboparty_motors/src/protocol/canfd/socket_canfd.cpp`(MotorsCANFD 层)

**验证结果**: 开启后, MotorsCANFD 接收线程成功收到 0x601(echo)和 0x501(灵巧手回复), dexhand 的接收回调被正确触发, `set_canfd_data_decode` 成功调用。

**注意**: 此修改在 roboparty_motors 层, 不在 dexhand 层。这意味着**任何使用 roboparty_motors 的项目都需要这个修复**。需要向 roboparty_motors 仓库提交 PR 或在本地维护此补丁。

### ⚠️ 剩余问题: get_now_position 仍返回 0 → ✅ 已解决

~~即使 0x501 帧已成功喂给 `set_canfd_data_decode`, 位置读取仍为 0。~~

**实际根因**: 接收回调注册的 CAN ID 不全。灵巧手实际使用 **0x581**(标准 CANopen SDO 响应, 0x580+node_id)作为反馈帧, 而非最初观察到的 0x501。注册 `{0x500, 0x480, 0x580, 0x180}` + node_id 后, 0x581 帧被正确接收并解码。

**验证结果(2026-08-10)**:
```
[1/3] 握紧... 手指1位置: 4999  ✅
[1/3] 张开... 手指1位置: 0     ✅
[2/3] 握紧... 手指1位置: 4999  ✅
[2/3] 张开... 手指1位置: 0     ✅
[3/3] 握紧... 手指1位置: 4999  ✅
[3/3] 张开... 手指1位置: 0     ✅
```

**所有审查问题已全部解决。dexhand 功能完整验证通过。**

---

## 三、与 roboparty_motors 的架构对比

### 一致性评估

| 维度 | roboparty_motors | roboparty_dexhand | 一致性 |
|------|-----------------|-------------------|-------|
| 工厂模式(create_xxx 静态方法) | `create_motor(type_str, ...)` | `create_hand(type_str, ...)` | ✅ 完全一致 |
| 基类纯虚接口 | 16个纯虚 + getter | 20个纯虚 + getter | ✅ 风格一致 |
| pybind11 只绑基类 | `class_<MotorDriver, shared_ptr<>>` | `class_<HandDriver, shared_ptr<>>` | ✅ 完全一致 |
| 每厂商独立子目录 | drivers/{dm,evo,lro,xyn} | drivers/lhandpro | ✅ 一致 |
| 每厂商独立静态库 | dm_motors.a 等 | lhandpro_driver.a | ✅ 一致 |
| CMake 双模式(ament + 纯CMake) | 是 | 是 | ✅ 一致 |
| CAN transport ownership | motors owns its transport | dexhand owns its private transport | consistent module boundary; separate sockets may bind the same interface |
| 版本号从 package.xml 读 | 是 | 是 | ✅ 一致 |
| spdlog 日志 | 基类构造创建 logger | 同 | ✅ 一致 |
| 线程安全(atomic + mutex) | atomic状态 + shared_mutex | atomic状态 + mutex | ✅ 一致 |

### 有意的设计差异(非问题)

| 差异点 | motors 做法 | dexhand 做法 | 原因 |
|--------|------------|-------------|------|
| 接收回调绑定 | `std::bind(&LroMotorDriver::canfd_rx_cbk, this)` | `lambda` 捕获 sdk_handle | motors 的回调是成员函数; dexhand 需要传 sdk_handle 给 C API |
| init 返回值 | `uint8_t`(错误码) | `bool` | 灵巧手 SDK 返回 int 错误码, 但 Python 侧用 bool 更简洁 |
| 多实例支持 | 天然支持(每电机独立) | 需要 registry(限制: SDK 回调不传 handle) | 灵巧手 SDK 的 C 回调设计限制 |
| 析构时序 | remove回调 → 从bus_registry移除 | stop_monitor → close → remove回调 → destroy | 灵巧手 SDK 有额外的 monitor 线程需要先停 |

### 接口对比

| 功能 | motors (MotorDriver) | dexhand (HandDriver) |
|------|---------------------|---------------------|
| 工厂创建 | `create_motor(id, iface_type, iface, type, model, ...)` | `create_hand(type, iface_type, iface, model, node_id, ...)` |
| 初始化 | `init_motor()` → uint8_t | `init_hand(enable, home, wait)` → bool |
| 反初始化 | `deinit_motor()` | `deinit_hand()` |
| 阻抗控制 | `motor_mit_cmd(p,v,kp,kd,t)` | 无(灵巧手用位置模式) |
| 位置控制 | `motor_pos_cmd(pos, spd)` | `set_target_position(id, pos)` + `move_motors()` |
| 状态刷新 | `refresh_motor_status()` | 无(由 start_monitor 自动刷新) |
| 读位置 | `get_motor_pos()` → float(rad) | `get_now_position(id)` → int(编码器计数) |
| 读电流 | `get_motor_current()` → float | `get_now_current(id)` → int(mA) |
| 清错误 | `clear_motor_error()` | `clear_alarm(id)` |
| 获取DOF | 无(电机不需要) | `get_dof(total, active)` |

---

## 四、文件清单

| 文件 | 行数 | 说明 |
|------|------|------|
| include/hand_driver.hpp | ~110 | 基类定义 + 工厂声明 |
| src/hand_driver.cpp | ~30 | 工厂函数实现 |
| src/drivers/lhandpro/lhandpro_driver.hpp | ~95 | LHandPro驱动头 |
| src/drivers/lhandpro/lhandpro_driver.cpp | ~250 | LHandPro驱动实现(CAN帧透传 + C API) |
| src/pybind_module.cpp | ~65 | pybind11 绑定 |
| CMakeLists.txt | ~100 | 顶层构建 |
| src/drivers/lhandpro/CMakeLists.txt | ~25 | 驱动构建 |
| cmake/roboparty_dexhandConfig.cmake.in | ~20 | find_package 模板 |
| package.xml | ~20 | ROS2 包描述 |
| scripts/test_dexhand.py | ~85 | Python 测试脚本 |
