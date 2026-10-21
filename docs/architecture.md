# 架构与开发

安装和运行见 [使用指南](getting-started.md)，接口语义见 [API](api.md)。

## 分层

```text
C++ 应用 / Python 绑定 / ROS 2 适配
                 ↓
             Hand 公共 API
                 ↓
          Session 会话与状态
           ↙             ↘
      Protocol        CanTransport
                          ↓
                 SocketCAN / WinUSB
```

| 位置 | 职责 |
| --- | --- |
| `include/rphand/` | `hand.hpp` 定义公共 API，`types.hpp` 定义状态和连接配置 |
| `src/hand.cpp` | 同步调用、任务调度、初始化、动作确认、重试和取消 |
| `src/rphand/session.*` | 接收、SDO 事务、反馈快照与命令缓存 |
| `src/rphand/protocol.*` | 编码、解码、长度校验和单位换算，不执行 I/O |
| `src/rphand/transport.*`、`backends/` | 通用 CAN 帧接口与平台收发实现 |
| `python/`、`ros2/` | 语言/框架适配，复用 C++ 核心 |
| `examples/` | 命令行解析与动作编排，不属于公共 SDK |

只安装公共头文件，不暴露平台类型。固定速率在 C++ `ConnectionOptions` 中定义，Python 直接复用。
轨迹、配置文件和系统网络设置留在应用层；核心不自动规划运动或提升权限。

## 内部约定

- 每个 Hand 会话使用控制工作线程与接收线程。普通请求串行执行，忙时拒绝，不积压位置目标。
- `stop`、`disable`、`prepare_disconnect` 优先执行，使旧任务失效；取消仍需等待当前底层调用返回。
  `disconnect` 只取消任务并释放资源，不发送停止或失能。销毁前调用方应结束使用它的其他线程。
- SDO 按节点、索引、子索引和应答类型匹配；Session 不增加重试，Hand 负责阶段和动作重试。
- 连接将反馈周期写入并读回为 20 ms，再观察 1.2 秒：两组各至少 40 个样本，频率 40..60 Hz，
  最新样本与相邻间隔均小于 100 ms。反馈有效性规则见 API。
- 位置组 `0x50` 与状态组 `0x5A` 独立更新；位置帧内部状态不覆盖公共状态组。
  命令参数按脏标记补发，回零后的首次运动先恢复控制状态和参数。
- Linux 要求接口已 UP 且支持 CAN FD；同一进程禁止重复占用同一接口/节点，不提供跨进程独占。
- Windows 按序列号独占整个适配器，当前仅支持 A8FA:8598 单通道。精确计算位时序，
  采样点接近 80%；不提供自定义 SJW、终端电阻配置或自动 bus-off 恢复。
- USB 接收等待 20 ms，发送和发送槽等待各 100 ms，控制请求 1 s；跨读取缓存拆帧，
  不扫描损坏数据猜测帧头。总线错误、溢出、异常帧或设备移除使反馈失效。
  正常 error-active 和零错误计数通知不当作灵巧手反馈。USB 提交成功不等于设备应答。

| 报文 | CAN ID |
| --- | --- |
| 控制/配置 | `0x500 + node_id` |
| 周期反馈 | `0x480 + node_id` |
| SDO 请求/应答 | `0x600 + node_id` / `0x580 + node_id` |

## 构建

| CMake 开关 | 默认值 | 用途 |
| --- | --- | --- |
| `RPHAND_BUILD_DEMO` | ON | 状态读取与 wave C++ 示例 |
| `RPHAND_BUILD_PYTHON` | OFF | pybind11 扩展 |
| `RPHAND_BUILD_TESTS` | OFF | 无硬件回归测试 |
| `BUILD_SHARED_LIBS` | OFF | 共享 SDK，部署时需携带运行库 |

旧 `RPHAND_BUILD_SMOKE` 兼容为测试开关别名。Windows 另构建 USB probe。
Linux 的 `bash build.sh cpp|python|ros|all` 分别安装到 `install/cpp|python|ros`；
`RPHAND_BUILD_JOBS` 默认 2，`RPHAND_PYTHON` 默认 `/usr/bin/python3`。
独立 Python 构建需要预装 pybind11，再用 `source setup_python.sh` 设置包路径；
ROS 构建需要 `/opt/ros/jazzy`，详见 [ROS 2](ros2.md)。

## 测试

```bash
cmake -S . -B build/check -DRPHAND_BUILD_TESTS=ON
cmake --build build/check --config Release --parallel 2
ctest --test-dir build/check -C Release --output-on-failure --parallel 2
python -m unittest discover -s tests -p test_python.py -v
```

Python 绑定测试针对已安装的包，修改核心后先重新安装。示例测试需要 Python 解释器，
缺少时 CMake 会提示跳过。测试目录只保留以下 7 个文件：

| 文件 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 回归测试构建；单独配置时验证安装后的 SDK |
| `transport.cpp` | 公共连接校验、离线行为、失败释放及 USB 帧/位时序 |
| `session.cpp` | 协议、模拟设备、命令缓存、状态与 SDO 事务 |
| `hand_cleanup.cpp` | 模拟传输下的真实 Hand 清理和重试 |
| `example_driver.cpp` | 用假 Hand 运行实际 C++ 示例，提供轨迹和参数检查 |
| `test_python.py` | Python 绑定及跨语言示例检查，共用一个入口 |
| `ros_lifecycle.py` | 有 ROS 环境时单独运行的节点生命周期检查 |

CTest 运行 transport、session、hand_cleanup、examples 四组；Python unittest 运行绑定测试。
不重复验证底层重试日志文本。跨语言轨迹允许半整数舍入处最多 1 个行程当量误差。
两个 C++ 模拟入口分别替换传输层和 Hand 层，保持分开以免混用真实与模拟设备实现。

安装包检查复用实际 `read_state.cpp`，无需额外的示例项目：

```bash
cmake -S tests -B build/consumer -DCMAKE_PREFIX_PATH=/absolute/path/to/installed/sdk
cmake --build build/consumer --config Release --parallel 2
ctest --test-dir build/consumer -C Release --output-on-failure
```

它通过 `find_package` 编译、链接并执行 `--help`，不访问设备。
CI 在 Windows/Linux 使用 Python 3.10 运行无硬件验证；实机连接、运动、拔插仍需目标平台验收。

## 源码发布包

`pyproject.toml` 的 `sdist.include` 明确列出源码、构建配置、示例、测试和文档范围，
其余文件默认不收录。新增源码目录或文件类型时同步更新该清单。
本地环境、编译产物、临时文件不应依靠 Git 是否跟踪来决定是否发布。

```bash
python -m pip install "scikit-build-core>=0.10" "pybind11>=2.13"
python tests/test_python.py --sdist dist
```

检查会在临时目录解包、注入构建垃圾并重新打包，确认无 Git 环境下内容仍一致。
CI 从这个源码包安装 Python 扩展并运行绑定测试，验证发布包能够独立构建。
