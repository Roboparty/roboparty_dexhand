# roboparty_dexhand_sdk

Linux SocketCAN CAN FD 下的 DH116S 六轴 C++17 / Python SDK，面向 ARM64 板部署。底层使用厂家 **2026-09-21 LHandProLib**。纯 C++ 构建不依赖 Python 或 ROS。

提供连接、使能、失能、回零、停止、单轴/六轴位置、速度、电流上限、力矩到位行为和状态读取，以及双手四指波浪 demo。公共接口以 [头文件](include/dh116s/hand.hpp) 和 [第一版接口说明](docs/v1_api_summary.md) 为准，失败返回 `false` 并向 stderr 写日志。

Python 通过 pybind11 复用同一 C++ 驱动，安装和使用见下方第 7 节；接口与线程约定见 [Python SDK 对接说明](docs/python_sdk_plan.md)。

## 1. 复制到 ARM 板

复制整个 `roboparty_dexhand_sdk/` 源码目录即可，不需要复制本机生成的 `build/`。项目已包含厂家头文件及 ARM64、x86_64 动态库，无需另建厂家 SDK 目录：

```text
roboparty_dexhand_sdk/
  CMakeLists.txt
  include/ src/ examples/ cmake/ docs/ tests/
  third_party/lhandpro/
    include/LHandProLib/
    aarch64/lib/libLHandProLib.so
    x86_64/lib/libLHandProLib.so
```

CMake 按目标架构自动选择项目内的库。ARM 板 `uname -m` 应为 `aarch64`。32 位 ARM 暂不支持，需要厂家另供匹配库。

## 2. 板上编译（推荐）

系统依赖：Linux、CMake >= 3.16、支持 C++17 的 GCC/Clang、pthread。厂家库已随项目提供，构建时无需下载依赖。

```bash
sudo apt-get install build-essential cmake iproute2 can-utils
cd ~/roboparty_dexhand_sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/dh116s_wave_demo --help
./build/dh116s_wave_demo --dry-run
```

不再使用 `LHANDPRO_SDK_ROOT`，旧缓存中的该变量也不会参与构建。编译时自动把对应 `.so` 复制到 `build/lib/`，demo 优先通过相对路径加载它，无需设置 `LD_LIBRARY_PATH`，也不依赖 `/opt/roboparty` 或 `~/dh116s_vendor`。移动运行文件时保留整个 `build/`，不能只移动可执行文件。切换 CPU 架构时使用新的构建目录。

也可以生成单独部署目录，安装会一起带上厂家库：

```bash
cmake --install build --prefix "$PWD/install"
./install/bin/dh116s_wave_demo --dry-run
```

如出现 `GLIBC_*` / `GLIBCXX_* not found`，需使用与板子系统兼容的厂家库/工具链；设置搜索路径不能解决 ABI 版本不匹配。

可选交叉编译（需要自行安装工具链，且 sysroot 与目标板兼容）：

```bash
cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j2
```

## 3. 配置 CAN FD

SDK 只绑定已有接口，不修改系统网络配置。先按实际硬件配置波特率、供电、接线和终端电阻，关闭控制同一只手的上位机/其他程序。

以下仅示例仲裁域 1 Mbps、数据域 5 Mbps，**必须与手和适配器的实际配置一致**；接口名也需替换：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
sudo ip link set can3 down
sudo ip link set can3 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can3 up
ip -details -statistics link show can0
ip -details -statistics link show can3
```

支持两个独立 CAN 接口各使用节点 1，也支持同一接口的两个不同节点。节点 ID 范围 `1..127`；同一总线的两个设备不能使用相同节点 ID。左右手标签由 demo 参数指定，SDK 不自动识别或交换左右手。

## 4. 双手四指波浪 demo

不带 `--dry-run` 时会真实使能、回零并运动。回零会运动六轴，包括拇指；波浪阶段只有食指、中指、无名指、小指运动，两个拇指通道的目标固定为 0。初次测试保持手周围无障碍，使用较小行程和合适电流。

```bash
./build/dh116s_wave_demo --left can0 1 --right can3 1 \
  --speed 20000 --current 400 --amplitude 6000 \
  --period-ms 3000 --cycles 10 --retries 3
```

同总线示例：`--left can0 1 --right can0 2`。

以上也是 demo 默认参数：四指目标范围 0..6000，周期 3 秒，运行 10 个周期，波浪阶段约 30 秒。速度 20000 当量/秒、电流上限 400‰ 来自项目内 LHandProLib 2026-09-21：创建全新实例、设置 DH116S 手型、启用厂家 dry-run 并初始化后，未调用参数 setter，六轴 `get_position_velocity()` 均为 20000，`get_max_current()` 均为 400；已在 x86_64 和 ARM64 上核对一致。它们是厂家 SDK 默认目标缓存，不是设备 EEPROM 出厂配置或实际速度/电流。此核对不发送硬件命令。

首次接线可以先只检查连接和反馈，不发送使能、回零、运动、停止或失能命令：

```bash
./build/dh116s_wave_demo --left can0 1 --right can3 1 --connect-only
```

该模式会配置设备反馈周期并验证两类反馈频率，然后断开。固件可用 8 槽位的 `0x50` 和 31 槽位的 `0x5A` 帧上报；SDK 校验完整长度后读取前六个主动轴，不要求帧槽位数恰好为 6。

运行顺序：

1. 并行连接两只手，配置六轴 20 ms 反馈，分别检查 `0x50` 和 `0x5A` 频率。
2. 设置速度、电流上限和力矩到位停止模式。
3. 使能并检查六轴实际使能反馈。
4. 整手回零，等待每轴本次进入回零运行并完成，然后恢复位置模式。
5. 调用一次单轴接口，将食指目标设为 0。
6. 以约 50 Hz 下发六轴目标，四指相差四分之一周期，左右手的相位传播方向相反；开始和结束使用平滑幅度过渡。
7. 回到位置 0 并等待位置/速度反馈满足容差，然后停止、失能、断开。

`--amplitude` 是四指目标最大值，范围 `1..10000`；`--period-ms` 是一个往返周期。速度小于波形要求时设备会跟不上目标，demo 不承诺轨迹跟踪精度。每秒打印一次实际位置；反馈过期、失能、失去回零状态或有告警会结束波浪并执行清理。

`Ctrl+C` / `SIGTERM` 触发停止、失能和断开；失能不依赖停止成功。停止/失能未确认时返回失败并记录日志，不能当作设备已经停止。`SIGKILL` 或掉电不能执行软件清理。退出码：`0` 完成，`1` 运行失败，`2` 参数错误，`130` 信号中断。

`--dry-run` 仅生成并检查波形范围、打印目标，不打开 CAN、不访问厂家设备，也不模拟设备反馈。构建过程不会自动运行运动 demo。

## 5. 在自己的 C++ 项目里使用

推荐通过 CMake 子目录引用：

```cmake
set(DH116S_BUILD_DEMO OFF CACHE BOOL "")
add_subdirectory(roboparty_dexhand_sdk)
add_executable(my_hand_app main.cpp)
target_link_libraries(my_hand_app PRIVATE roboparty::dexhand_sdk)
```

库产物为 `libroboparty_dexhand_sdk.a`（默认静态库）。DH116S 设备接口仍使用 `dh116s::Hand` 和 `<dh116s/hand.hpp>`；旧 CMake 别名 `dh116s::sdk` 仍可使用。

最小流程（会驱动设备）：

```cpp
#include <dh116s/hand.hpp>

int main() {
    dh116s::Hand hand;
    if (!hand.connect("can0", 1, 3)) return 1;
    bool ok = hand.set_speed(20000)
        && hand.set_current_limit(400)
        && hand.set_torque_reached_mode(true)
        && hand.enable()
        && hand.home();
    if (ok) ok = hand.set_positions({0, 0, 1000, 1000, 1000, 1000});
    // Submission is not arrival. Add a feedback loop with a deadline here
    // before proceeding to the next action; see wave_demo.cpp.
    const auto state = hand.get_state();
    const bool stopped = hand.stop();
    const bool disabled = hand.disable();
    hand.disconnect();
    return ok && state.feedback_valid && stopped && disabled ? 0 : 1;
}
```

这个最小片段紧接着停止，完整的运动等待参见 [wave_demo.cpp](examples/wave_demo.cpp)。

### 接口与单位

| 接口 | 含义 |
| --- | --- |
| `connect(can, node, retry_count=30)` | 两阶段连接，各最多指定次数；不自动使能/回零 |
| `disconnect()` | 取消本地操作，回收线程、socket、厂家对象；不自动停止/失能 |
| `is_connected()` | 本地连接状态，不保证设备在线或反馈新鲜 |
| `enable()` / `disable()` / `stop()` | 只有六轴满足反馈条件才返回 true；失败/部分完成返回 false |
| `home()` | 等待本次六轴回零完成，不自动使能 |
| `set_positions(array<int,6>)` | 六轴绝对目标，范围 0..10000；不等待到位 |
| `set_position(id, position)` | 单轴绝对目标，ID 1..6 |
| `set_speed(speed)` | 六轴速度，正整数，厂家行程当量/秒；具体可用上限由厂家库限制 |
| `set_current_limit(current)` | 六轴电流上限，0..3000，厂家千分比，不是 mA |
| `set_torque_reached_mode(true/false)` | true=力矩到位停止，false=保持设定电流 |
| `get_state()` | 立即复制最新缓存，不发查询、不等待下一帧 |

数组固定顺序：**拇指侧摆、拇指屈伸、食指屈伸、中指屈伸、无名指屈伸、小指屈伸**，对应 ID 1..6。位置 0..10000 为厂家行程当量，不是角度。

`HandState` 包含 `positions`、`velocities`、`currents`、`enabled`、`homed`、`torque_reached`、`fault_codes` 六轴数组和 `feedback_valid`。速度、电流为实际反馈；报警为 `0x5A` 状态2低四位。两类反馈都收到且各自年龄小于 100 ms，连接正常、接收未失败时，`feedback_valid` 才为 true。过期时仍保留最后数值；位置与状态来自不同帧，不代表同时采样。

位置接口的 true 只说明厂家调用成功且调用期间发送层未报告失败。速度/电流/力矩模式接口还检查厂家配置缓存一致，但都不表示设备已独立 ACK、到位或参数已实际生效。不提供参数 getter、故障清除、设备查询接口。

### 线程与等待

每个实例独占厂家对象、控制线程、接收线程和 socket。普通控制接口同步返回，不建立运动长队列；普通操作已占用时新普通操作返回 false（Busy）。`get_state()` 可并发读取；另一线程可以调用 `stop()`、`disable()`、`disconnect()` 取消旧等待和待执行操作。新的停止/失能请求会取消较早的操作，因此用户仍应顺序调用 stop、disable。

厂家调用无法强制打断，优先请求在正在执行的厂家调用返回后生效。对象析构前应先结束所有使用该对象的用户线程；Hand 不可复制或移动。同进程拒绝重复控制同一 `(接口名, 节点 ID)`，不跨进程仲裁。

固定内部参数：使能/失能/停止各最多 30 次，每次反馈等待 500 ms；回零未观察到启动的轴每秒补发一次，每轴最多 30 次，整个回零最多 60 秒；参数设置最多 30 次。厂家函数自身耗时可能使实际墙钟时间更长。反馈过期仍允许尝试停止/失能。

连接的第二阶段通过原始 SDO 对六轴周期写入 200（0.1 ms 单位）、匹配写应答、读取并核对，再配置上报倍数 1；每类采样约 1.2 秒、至少 40 帧，验收 40..60 Hz 且间隔小于 100 ms。只改运行配置，不写 EEPROM。失败可能已改变部分设备反馈周期，不会自动回滚。

SDO 无事务序号，超时后等待 500 ms 排空迟到回复；极迟的同索引回复不能从协议上完全区分，因此同时要求读回和实际频率通过。不要让其他程序并行配置同一设备。`stop_monitor()` 后主动反馈解码、重复命令的设备触发行为和 `stop_motors()` 后再次运动的恢复行为均需实机确认，本版不猜测性补发急停撤销命令。

## 6. 简单验证与排查

可选的小型检查，不需要硬件、不执行运动：

```bash
cmake -S . -B build -DDH116S_BUILD_SMOKE=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/dh116s_wave_demo --dry-run
```

已在本机 x86_64 和 ARM64 板上完成编译、基本参数/离线生命周期检查和波形 dry-run。板上 can0/can3 节点 1 的实际连接检查已通过，两类反馈均约 50 Hz；**尚未验证真实手部运动**。没有增加大型测试框架。

| 现象 | 检查方向 |
| --- | --- |
| CAN open failed | 接口名、UP 状态、FD MTU、适配器驱动与权限 |
| initial/model check failed | 节点、供电、波特率、接线，以及实际手型是否 DH116S 六轴 |
| SDO timeout/abort | 日志中的轴索引/应答值，固件周期对象支持，是否有其他控制进程 |
| feedback frequency failed | 两类实测频率、总线负载、周期配置、主动上报支持 |
| feedback missing/stale | CAN 错误统计、BUS-OFF、供电/线缆、调度延迟；超过 100 ms 不确认动作成功 |
| home start not observed | 本次回零运行位未被观察到；不会用旧 homed=true 冒充成功 |
| stop/disable not confirmed | 查看实际轴状态与供电，不能凭函数调用认定已停机 |

设计历史保留在 `docs/`；`cpp_v1_implementation_plan.md` 内存在旧伪代码，当前实际接口以头文件和本文为准。

## 7. Python SDK 与 demo

### 系统 Python 本机编译（无需 pip 或虚拟环境）

在目标板上准备系统依赖后，直接用 CMake 编译：

```bash
sudo apt-get install build-essential cmake python3-dev pybind11-dev
cd ~/roboparty_dexhand_sdk
bash build_python.sh
source setup_python.sh
/usr/bin/python3 -m unittest discover -s tests -p 'test_python_*.py' -v
```

编译脚本固定使用 `/usr/bin/python3`，输出到 `build-python-system/`，完整包安装在项目内 `install/python/`，不修改系统 Python 目录。`setup_python.sh` 设置当前 Bash 的 `PYTHONPATH`，无需设置 `LD_LIBRARY_PATH`；新终端需要重新 source。源码更新后重新执行编译脚本即可。

启动双手波浪（会使能、回零并运动，运行前清空运动空间）：

```bash
# 配置环境后直接执行系统 Python。
cd ~/roboparty_dexhand_sdk
source setup_python.sh
/usr/bin/python3 examples/wave_demo.py
```

### pip 安装（可选）

支持 Linux x86_64 / ARM64、Python >= 3.9。在目标机器安装，需要 C++17 编译器、CMake 和 Python 开发文件：

```bash
sudo apt-get install build-essential cmake python3-dev python3-venv
cd ~/roboparty_dexhand_sdk
python3 -m venv .venv
. .venv/bin/activate
python -m pip install .
python -m unittest discover -s tests -p 'test_python_*.py' -v
```

pip 自动安装构建依赖 `scikit-build-core` 和 `pybind11`；离线环境需提前准备依赖。wheel 内包含 `_core` 扩展、类型提示及 `lib/libLHandProLib.so`，通过 `$ORIGIN/lib` RPATH 优先加载随包厂家库，无需设置 `LD_LIBRARY_PATH`。wheel 按 CPU 架构和 Python ABI 分别构建，不承诺跨系统版本兼容。

如果 Conda 环境导入时报 `GLIBCXX_3.4.30 not found`，说明其 C++ 运行库与系统编译器不兼容。可使用 `/usr/bin/python3 -m venv .venv` 创建系统 Python 环境，或更新 Conda 的 C++ 运行库；重新设置厂家库路径无法解决此问题。

完成第 3 节 CAN 配置后再运行下面的运动示例。

初次阅读 SDK 建议从 [wave_demo.py](examples/wave_demo.py) 开始。这个约 70 行的双手波浪示例带中文注释，直接用 `handleft`、`handright` 演示连接、参数设置、使能、回零和位置下发。四指以相反方向形成波浪，拇指保持零位，结束后停止、失能并断开。修改 `connect()` 中的接口/节点，以及文件顶部的 `AMPLITUDE`、`PERIOD`、`CYCLES` 即可调整设备和波浪动作。

```bash
# 会使能、回零并运动双手，运行前清空运动空间。
python examples/wave_demo.py
```

Python demo 采用同步调用，`home()` 等阻塞调用期间 Ctrl+C 可能延迟处理；只检查命令返回值，不检测实时反馈或确认到位。不接受命令行参数，包括旧版的 `--dry-run` 和 `--connect-only`，传入参数时直接退出。需要完整反馈监测、连接检查和中断处理时参见第 4 节的 C++ demo。

Python demo 与 C++ 默认波浪参数一致：速度 20000、限流 400、幅值 6000、周期 3 秒、运行 10 个周期（30 秒），首尾采用 2 秒余弦渐变，目标取整方式也保持一致。修改文件顶部的 `SPEED`、`CURRENT`、`AMPLITUDE`、`PERIOD`、`CYCLES` 可调整参数。仅检查连接而不运动可使用下面的 SDK 片段：

```python
from roboparty_dexhand_sdk import Hand

hand = Hand()
try:
    if not hand.connect("can0", 1, retry_count=3):
        raise RuntimeError("connect failed")
    state = hand.get_state()
    if state.feedback_valid:
        print(state.positions, state.fault_codes)
finally:
    hand.disconnect()  # 此例只检查连接，没有使能或运动。
```

`Hand` 方法名、单位及 bool 返回值与 C++ 一致；`set_positions()` 接受六个整数的 list/tuple。类型错误抛 `TypeError`，长度错误或超出 C++ int 表示范围抛 `ValueError`；拒绝 float、bool 充当整数。可表示但超出设备参数范围的值由 C++ 校验并返回 `False`。状态字段只读，六轴字段为独立 tuple 快照，必须检查 `feedback_valid`。

底层调用和对象析构等待会释放 GIL，但主线程直接调用阻塞方法时，Python 信号处理仍可能延迟到方法返回。Python demo 在单个应用线程顺序调用双手接口，捕获 Ctrl+C 后退出码为 130，并在 finally 中停止、失能和断开双手。停止失败仍尝试失能；无法确认停止/失能时报告失败。没有 SIGTERM 处理。自定义运动程序也应显式执行清理，不能依赖析构停机。

无硬件测试覆盖参数转换、离线返回、快照、双手波形、调用失败和模拟 KeyboardInterrupt 后的清理。已在 ARM64 板使用系统 Python 3.12.3 完成本机编译、包内厂家库加载验证和 10 项无硬件测试；真实回零取消与物理停机仍需实机验证。
