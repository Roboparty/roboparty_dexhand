# rphand 架构

本 SDK 在 Linux 上通过 SocketCAN CAN FD 控制六轴灵巧手。C++、Python 和 ROS 共用同一套 C++ 实现，协议处理直接从源码构建，不依赖旧的预编译设备库。

文档入口：[C++ / Python API](api.md) · [ROS](ros2.md)。本文对应 [CMakeLists.txt](../CMakeLists.txt) 和 [src](../src) 中的当前实现。

## 代码分层

```text
C++ 应用             Python 应用             ROS 节点
    |                   |                      |
    |           python/bindings.cpp            |
    +-------------------+----------------------+
                        |
                 rphand::Hand
                        |
                     Session
                    /       \
               Protocol   SocketCAN
                              |
                         Linux CAN FD
```

| 位置 | 职责 |
| --- | --- |
| [include/rphand/hand.hpp](../include/rphand/hand.hpp) | 公共 `Hand` 和 `HandState`，安装给 C++ 调用方 |
| [src/hand.cpp](../src/hand.cpp) | 同步 API、任务调度、连接流程、动作确认、重试与取消 |
| [src/rphand/session.hpp](../src/rphand/session.hpp)、[session.cpp](../src/rphand/session.cpp) | 设备会话、接收线程、SDO 事务、命令缓存与反馈快照 |
| [src/rphand/protocol.hpp](../src/rphand/protocol.hpp)、[protocol.cpp](../src/rphand/protocol.cpp) | 报文编码、解码、长度校验和数据换算，不执行 I/O |
| [src/rphand/socketcan.hpp](../src/rphand/socketcan.hpp)、[socketcan.cpp](../src/rphand/socketcan.cpp) | Linux socket、节点过滤、CAN FD 帧收发和发送错误计数 |
| [python/bindings.cpp](../python/bindings.cpp) | Python 参数检查、GIL 释放、状态快照转换 |
| [ros2/rphand_ros](../ros2/rphand_ros) | 会话重建、周期目标发送、状态发布、消息与示例 |

`src/rphand` 是内部实现，头文件不安装。应用通过 `rphand::Hand` 使用设备，不直接操作 Session 或协议缓存。

## 线程与请求

每个已启动的 Hand 会话使用一个控制工作线程和一个接收线程。公开控制方法提交任务后等待结果，因此 C++ 和 Python 调用都是同步的。

- 普通操作串行执行。已有普通操作或紧急任务时，新普通请求返回失败，不积压位置目标。
- `stop()`、`disable()`、`prepare_disconnect()` 进入优先队列，使旧任务的操作代次失效，并唤醒等待。
- `disconnect()` 取消本地任务，等待工作线程释放会话；它不发送停止或失能命令。
- 接收线程独立处理 SDO 应答和反馈，`get_state()` 读取受锁保护的快照。

取消在检查操作代次时生效；正在执行的底层调用仍需返回。同一进程不允许两个 Hand 占用相同接口和节点，但没有跨进程独占。销毁对象前，调用方应先结束使用它的线程。

## 连接与通信

连接依次打开 CAN、初始化并检查设备型号和六轴能力、配置反馈、启用底层无回零位置命令选项，再检查是否需要恢复遗留软件急停。恢复时先确认失能及静止，再发送撤销急停所需的控制边沿。连接不会主动使能或回零，但可能改变设备状态。

反馈配置对六轴寄存器写入并读回周期值，目标周期为 20 ms。随后观察 1.2 秒：两组各至少 40 个样本，频率在 40..60 Hz，最新样本及相邻间隔均小于 100 ms。

SDO 用于参数读写，执行“请求一次、等待对应应答”的事务。Session 按节点、索引、子索引和应答类型匹配，事务串行进行；参数读取和反馈配置共用等待逻辑。SDO 层本身不增加重试，动作与阶段重试由 Hand 决定，次数见 [API](api.md)。

SocketCAN 要求接口已存在、处于 UP 状态且具有 CAN FD MTU。接口速率由系统配置，SDK 不设置。发送启用 BRS，长度补齐到合法 CAN FD 长度，最长 64 字节。

| 报文 | CAN ID |
| --- | --- |
| 控制和配置 | `0x500 + node_id` |
| 周期反馈 | `0x480 + node_id` |
| SDO 请求 | `0x600 + node_id` |
| SDO 应答 | `0x580 + node_id` |

## 反馈与命令缓存

位置组 `0x50` 更新位置、速度和电流；状态组 `0x5A` 更新使能、回零、力矩到位和故障码。两组分别记录接收时间和序号，不是同一时刻的硬件采样。位置帧中的状态信息供内部运动检查使用，不覆盖公共状态位。

只有连接有效、接收正常、两组都已收到且各自年龄小于 100 ms 时，`feedback_valid` 才为 true。失效后数组可能保留旧值。`is_connected()` 只是本地连接标志，不能替代反馈有效性检查。

Session 缓存六轴目标、速度和电流参数，以脏标记控制参数补发。回零后首次运动会先恢复控制状态、补发参数，再发位置目标。单轴设置只更新指定轴的目标缓存，实际位置报文仍包含六轴。

## 构建产物

要求 Linux、C++17 编译器、CMake 3.16 及以上和线程库。Python 另需解释器开发文件与 pybind11；ROS 依赖及入口见 [ROS](ros2.md)。以下命令均在 SDK 根目录执行。

| 命令 | 产物 |
| --- | --- |
| `bash build.sh cpp` | `install/cpp` 下的静态库、公共头文件、CMake 配置及 C++ 示例 |
| `bash build.sh python` | `install/python/rphand` 下的 Python 包和原生扩展 |
| `bash build.sh ros` | `install/ros` 下的 ROS 包，脚本要求 `/opt/ros/jazzy` |
| `bash build.sh all` | 依次构建以上三项，也是无参数时的行为 |

`RPHAND_BUILD_JOBS` 控制并行度，默认 2；`RPHAND_PYTHON` 指定 Python 构建解释器，默认 `/usr/bin/python3`。默认脚本静态链接 SDK 核心，Python 原生扩展和 ROS 消息类型支持仍是正常的共享库。

## 无硬件验证

```bash
cmake -S . -B build/check -DRPHAND_BUILD_SMOKE=ON -DRPHAND_BUILD_DEMO=ON
cmake --build build/check --parallel 2
ctest --test-dir build/check --output-on-failure
```

| 测试 | 覆盖内容 |
| --- | --- |
| `rphand_basic_smoke` | 公共 API 参数、离线操作、连接失败后的资源释放 |
| `rphand_canfd_backend` | 报文字节、初始化、参数补发、反馈边界与关闭取消 |
| `rphand_session_state` | 快照与过期、回零观察、SDO 匹配、串行和取消 |
| `rphand_hand_cleanup` | 停止条件已满足时跳过发送、单次退出清理、普通停止最多 30 次尝试 |
| `rphand_examples` | C++/Python 示例轨迹一致性、拇指全行程目标、阶段边界、循环和参数检查；需构建示例且可找到 Python 解释器 |

Session 测试共用 `tests/session_device.hpp` 中的模拟设备；Hand 清理测试使用独立的模拟传输，不改变生产代码的接口。

这些测试使用模拟应答或不可能存在的接口名，不控制实机。Python 与 ROS 的验证命令分别见另外两份文档；自动化检查不替代设备上的连接、回零、运动及断线验证。
