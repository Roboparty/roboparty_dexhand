# Python SDK 对接方案

第一版 Python SDK 已按本文实现，安装和运行命令见 [README](../README.md#7-python-sdk-与-demo)。底层接口以 [C++ 头文件](../include/dh116s/hand.hpp) 为准；实机验收仍需在目标设备执行。

## 1. 对接方式

使用 **pybind11** 将现有 C++ `dh116s::Hand` 和 `HandState` 暴露给 Python，包名为 `roboparty_dexhand_sdk`。

```text
Python 应用
    -> roboparty_dexhand_sdk Python 包
    -> _core 扩展模块（pybind11）
    -> 现有 C++ Hand
    -> 厂家 LHandProLib -> SocketCAN -> 灵巧手
```

Python 层只做参数转换、接口导出和使用示例。CAN 收发、反馈解析、重试、超时、动作确认继续复用 C++ 实现，不在 Python 里重新写一套驱动。

## 2. 第一版接口

Python 方法名、单位和动作语义与 C++ 一致：

| Python 接口 | 返回值及作用 |
| --- | --- |
| `Hand()` | 创建独立手实例，不自动连接 |
| `connect(can_name, node_id, retry_count=30)` | `bool`，连接并检查反馈 |
| `disconnect()` | `None`，释放本地资源，不自动停止或失能 |
| `is_connected()` | `bool`，查询本地连接状态 |
| `enable()` / `disable()` / `stop()` | `bool`，按 C++ 规则确认六轴状态 |
| `home()` | `bool`，等待本次回零完成 |
| `set_positions(values)` | `bool`，下发六轴目标，不等待到位 |
| `set_position(id, position)` | `bool`，下发单轴目标 |
| `set_speed(speed)` | `bool`，设置六轴速度 |
| `set_current_limit(current)` | `bool`，设置六轴电流上限 |
| `set_torque_reached_mode(stop_on_reached)` | `bool`，设置力矩到位停止或保持 |
| `get_state()` | `HandState`，返回最新反馈快照 |

`set_positions()` 接受长度为 6 的整数 list/tuple。数组顺序仍为：拇指侧摆、拇指屈伸、食指、中指、无名指、小指；位置范围 0..10000，单轴 ID 为 1..6。

`HandState` 提供只读属性：`positions`、`velocities`、`currents`、`enabled`、`homed`、`torque_reached`、`fault_codes` 和 `feedback_valid`。六轴字段返回 Python tuple，是独立快照，不暴露 C++ 内部缓存引用。反馈无效时保留最后数值，调用方必须检查 `feedback_valid`。

类型不匹配抛 `TypeError`，序列长度不为 6 或超出 C++ int 表示范围抛 `ValueError`；可转换为 C++ 参数的数值继续交给 C++ 校验，越界或设备操作失败返回 `False`，沿用 C++ 日志。绑定层检查整数表示范围，禁止隐式截断、浮点转整数或把 `bool` 当作位置整数。位置下发成功不等于设备已到位。

## 3. 线程与生命周期

**所有底层线程由 C++ 管理。** 每个 `Hand` 的控制线程、接收线程、反馈锁、取消和资源回收沿用现有实现；Python 不管理 CAN socket，也不直接调用厂家库。

- Python 接口保持同步：`home()` 等待完成后返回，不自动创建 Python 工作线程。
- pybind11 在可能阻塞的 C++ 调用期间释放 GIL，参数转换和返回值转换时持有 GIL。这样另一 Python 线程可以调用 `stop()`、`disable()` 或 `disconnect()`。
- 释放 GIL 不会允许并发操作绕过 C++ 的串行化和 Busy 判断。普通操作建议由一个应用线程顺序调用。
- Python 对象拥有 C++ `Hand`；调用期间保证对象存活，快照不依赖其生命周期。实现时需处理析构可能等待线程退出的问题，避免析构长时间持有 GIL。
- 用户显式执行 `stop()`、`disable()`、`disconnect()`，并先结束自己的调用线程再销毁对象。停止失败后仍应尝试失能；不能依赖垃圾回收保证物理停机。

两只手通过两个 `Hand` 对象控制。应用需要同时回零时可以使用 Python 线程；底层设备控制仍由各自的 C++ 线程执行。第一版不额外提供线程池或异步 API。

## 4. 构建和安装

建议使用 **scikit-build-core + pybind11 + 现有 CMake**：

```text
pyproject.toml                    Python 包与构建依赖配置
python/bindings.cpp               pybind11 绑定
python/roboparty_dexhand_sdk/
    __init__.py                  导出 Hand、HandState
    __init__.pyi                 接口类型提示
    py.typed                     标记类型提示可用
examples/wave_demo.py             Python 双手示例
```

增加默认关闭的 `ROBOPARTY_BUILD_PYTHON` 构建选项。纯 C++ 的现有编译方式不变，不要求 Python 或 pybind11；构建 Python 包时才启用该选项。被链接进 Python 扩展的 C++ 库需要开启位置无关代码（PIC）。

目标安装方式为在目标机器执行 `python -m pip install .`，得到以下包内容：

```text
roboparty_dexhand_sdk/
    __init__.py
    __init__.pyi
    py.typed
    _core.<Python ABI 和架构>.so
    lib/libLHandProLib.so
```

厂家库取自项目内 `third_party/lhandpro/`，根据目标架构选择，并随 wheel 安装。扩展使用相对路径加载包内配套库，沿用 C++ demo 的库优先级策略，避免系统中的旧库抢先加载；不依赖外部 SDK 目录或用户配置 `LD_LIBRARY_PATH`。

ARM64 与 x86_64 分别构建 wheel，Python ABI 也需匹配。第一版优先在 ARM 板本机安装验证，不承诺跨系统版本通用。系统仍需兼容的 C/C++ 运行库；构建时需要编译器、Python 开发文件及上述 Python 构建依赖，离线安装需提前准备这些依赖。

## 5. 示例与验收

提供带中文注释的 Python 双手波浪入门示例 `examples/wave_demo.py`，直接使用 `handleft`、`handright`，包含连接、参数设置、使能、顺序回零、双手反向波浪、停止、失能和断开。与 C++ 默认波浪参数一致：速度 20000、限流 400、幅值 6000、周期 3 秒、10 个周期共 30 秒，首尾余弦渐变和目标取整方式也一致，参数在源码中修改。旧版复杂 Python demo 已删除；当前示例不接受命令行参数，不提供 `--dry-run` 或 `--connect-only`，也不监测反馈或确认到位。用 `try/finally` 清理并处理 `KeyboardInterrupt`，已加入模拟调用失败与中断清理测试。主线程直接调用 SDK 阻塞方法时，释放 GIL 本身不能保证及时处理 Python 信号；需要并发取消的应用须自行安排调用线程。

保持验证简单：

1. 无硬件检查导入、参数转换、离线失败返回、重复断开和波形生成。
2. 在 ARM 板安装包，确认加载的是包内厂家库；即使系统存在旧库也能正常导入。
3. 使用 README 中的 SDK 片段检查连接，显式运行运动 demo，核对 Python/C++ 接口行为一致；有并发需求的应用另验证等待回零时其他 Python 线程能调用停止，以及中断后的清理。

第一版不增加 ROS、asyncio、反馈回调、轨迹规划或 NumPy 强依赖。Python 的约 50 Hz 目标更新仅作为示例，不承诺硬实时。
