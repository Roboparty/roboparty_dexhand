# 构建与集成

支持 Linux x86_64 和 ARM64。编译器需要 C++17，CMake >= 3.16。厂家库已在仓库中，构建无需另外下载厂家 SDK；系统依赖需预先安装。

## 依赖

```bash
sudo apt-get install build-essential cmake iproute2 can-utils
# Python 绑定
sudo apt-get install python3-dev pybind11-dev
# 可选 pip / wheel 环境
sudo apt-get install python3-venv
```

ROS 统一入口要求 /opt/ros/jazzy/setup.bash 和 colcon。安装好 Jazzy 及其 apt 源后：

```bash
sudo apt-get install python3-colcon-common-extensions ros-jazzy-ament-cmake \
  ros-jazzy-rclcpp ros-jazzy-rclpy ros-jazzy-rcl-interfaces \
  ros-jazzy-std-msgs ros-jazzy-std-srvs ros-jazzy-sensor-msgs \
  ros-jazzy-rosidl-default-generators ros-jazzy-rosidl-default-runtime \
  ros-jazzy-launch-ros ros-jazzy-ament-index-python
```

C++ 独立构建不依赖 Python 或 ROS。ROS 控制脚本使用 rclpy，不需要 Python SDK bindings。pip 构建的 Python 声明版本为 >=3.9，已实际构建检查过系统 Python 3.10 和 3.12。

## 统一入口

所有命令在仓库根目录运行：

```bash
bash build.sh cpp
bash build.sh python
bash build.sh ros
bash build.sh all
```

不传参数等同 all。三种构建各自编译底层源码，不共享二进制缓存；源码修改后执行 all 可同步更新。

| 内容 | 构建位置 | 安装位置 |
| --- | --- | --- |
| C++ SDK、demo | build/cpp | install/cpp |
| Python SDK | build/python | install/python |
| ROS 驱动、消息及脚本 | build/ros | install/ros |
| colcon 日志 | build/ros-log | 无 |

```bash
DEXHAND_BUILD_JOBS=2 bash build.sh all
DEXHAND_PYTHON=/usr/bin/python3 bash build.sh python
```

默认 Release、静态链接 C++ 核心；厂家库仍是共享库。默认不运行测试或硬件动作。构建失败会立即退出，之前已完成的安装不会自动撤销。

build_python.sh 与 wave_demo_50hz 文件已移除。旧 build-python-system 不再使用，旧的安装文件不会因为重新构建自动删除。

## 加载安装

C++：

```bash
./install/cpp/bin/dh116s_wave_demo --help
./install/cpp/bin/dh116s_wave_demo --rate 50 --dry-run
```

Python：

```bash
source setup_python.sh
python3 -c 'from roboparty_dexhand_sdk import Hand; print(Hand)'
```

setup_python.sh 为当前 Bash 增加项目 install/python 的 PYTHONPATH；用与编译时匹配的 Python 解释器运行。新终端需要重新 source。

ROS：

```bash
source /opt/ros/jazzy/setup.bash
source install/ros/local_setup.bash
ros2 pkg executables roboparty_dexhand_ros
```

不要在同一终端叠加多份同名 SDK/ROS 安装。重编译不会替换运行中节点，需要退出旧节点再启动新节点。

## 手动 CMake

```bash
cmake -S . -B build/cpp \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/install/cpp" \
  -DDH116S_BUILD_DEMO=ON
cmake --build build/cpp -j2
cmake --install build/cpp
```

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| DH116S_BUILD_DEMO | ON | C++ wave demo |
| DH116S_BUILD_SMOKE | OFF | 无硬件 API 检查 |
| ROBOPARTY_BUILD_PYTHON | OFF | pybind11 绑定 |
| BUILD_SHARED_LIBS | 未指定时为静态 | C++ 核心库类型 |

统一脚本显式选择上述选项。ROS 包内部以 add_subdirectory 编译仓库核心，必须携带完整 SDK 源码；不可只复制 ros2 子目录。

已有外部 colcon 工作区也可编译：

```bash
# 在含 src/roboparty_dexhand_sdk 的工作区根目录
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/roboparty_dexhand_sdk/ros2 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
source install/local_setup.bash
```

这种方式的 install 位于外部工作区，和统一脚本的 install/ros 二选一使用。

## 外部 C++ 项目

安装 SDK 后，在调用方已有可执行目标的 CMakeLists.txt 中使用：

```cmake
find_package(roboparty_dexhand_sdk CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE roboparty::dexhand_sdk)
```

配置时指定安装前缀：

```bash
cmake -S /path/to/your_app -B /path/to/your_app/build \
  -DCMAKE_PREFIX_PATH=/path/to/roboparty_dexhand_sdk/install/cpp
```

导出包会引入 Threads 和厂家库。也支持源码集成：

```cmake
set(DH116S_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(/path/to/roboparty_dexhand_sdk sdk)
target_link_libraries(your_app PRIVATE roboparty::dexhand_sdk)
```

独立应用再次安装/分发时，需要根据自己的目录布局配置共享库搜索路径；SDK 的 demo RPATH 不会自动替所有应用配置。

## pip 安装

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install .
```

pip 通过 scikit-build-core 和 pybind11 编译，可能下载构建依赖，离线环境需提前准备。wheel 带有绑定、类型提示和厂家库，按 CPU 架构及 Python ABI 构建，不承诺跨系统兼容。用 pip 安装时不需要再 source setup_python.sh。

## ARM64 交叉编译

仅适用于已具备兼容编译器和 sysroot 的环境，推荐优先在目标板本机编译：

```bash
cmake -S . -B build/arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64 -j2
```

工具链文件只选择 Linux/aarch64 和 aarch64-linux-gnu 编译器，不自动提供 sysroot，也不处理 ROS 或 Python 交叉编译环境。

## 运行时库与清理

CMake 自动选择 third_party/lhandpro 中目标架构的库。安装产物使用相对 RPATH 查找随包库，通常不需要 LD_LIBRARY_PATH。复制时保留整个安装目录，不要只复制可执行文件。

GLIBC/GLIBCXX 不兼容需要调整目标工具链或运行库；改搜索路径不能修复 ABI 不兼容。尤其应避免 Conda 运行库覆盖系统构建依赖。

完全重建前先退出依赖安装目录的程序，再在确认位于仓库根目录后执行：

```bash
rm -rf build install
bash build.sh all
```

这只适用于统一入口产生的本仓库目录，不会清理其他工作区产物。

## 可选无硬件检查

```bash
cmake -S . -B build/check -DDH116S_BUILD_SMOKE=ON
cmake --build build/check -j2
ctest --test-dir build/check --output-on-failure

source setup_python.sh
python3 -m unittest discover -s tests -p 'test_python_sdk.py' -v

cmake -S tests/cmake_consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH="$PWD/install/cpp"
cmake --build build/consumer -j2
./build/consumer/dexhand_install_check
```

另可运行两个 demo 的 --dry-run 检查目标生成。无硬件检查不能代替真实回零、运动、停止和异常恢复验证。
