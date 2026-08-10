# roboparty_dexhand

Dexterous hand driver library with factory pattern. Architecture mirrors `roboparty_motors`.

## 架构

```
roboparty_dexhand/
├── include/hand_driver.hpp        # 抽象基类 HandDriver + 静态工厂 create_hand()
├── src/
│   ├── hand_driver.cpp            # 工厂函数(根据 hand_type 字符串分发)
│   ├── pybind_module.cpp          # pybind11 绑定 → dexhand_py
│   └── drivers/
│       └── lhandpro/              # LHandPro (RP_Hand) 驱动
│           ├── lhandpro_driver.hpp/.cpp
│           └── LHandProLib.h      # 灵巧手 SDK C 头文件
├── thirdparty/lib/
│   └── libLHandProLib.so          # 灵巧手闭源 SDK
└── CMakeLists.txt
```

## 核心设计

### 工厂模式
```cpp
// C++
auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0");
```
```python
# Python
from dexhand_py import HandDriver
hand = HandDriver.create_hand(hand_type="LHandPro", interface_type="canfd", interface="can0")
```

### 与 arm 共享 CAN 总线
LHandProDriver 复用 `MotorsCANFD::get("can0")` 单例（来自 roboparty_motors），
和 arm 电机驱动共用同一个 SocketCAN socket。

```
libLHandProLib.so ←→ [CAN帧透传] ←→ MotorsCANFD单例 ←→ SocketCAN(can0)
                                          ↑
                          arm电机驱动也用这个socket
```

## 编译

```bash
# 方式1: roboparty_motors 已安装
mkdir build && cd build
cmake .. && make -j

# 方式2: roboparty_motors 未安装, 指定源码路径
mkdir build && cd build
cmake .. -DROBOPARTY_MOTORS_SOURCE_DIR=/home/sjh/leisai_hand/roboparty_motors
make -j
```

## Python 使用

```python
from dexhand_py import HandDriver

hand = HandDriver.create_hand(
    hand_type="LHandPro",
    interface_type="canfd",
    interface="can0",
    canfd_node_id=1)
hand.init_hand(enable_motors=True, home_motors=True, home_wait_time=6.0)

# 抓握
for j in range(1, 7):
    hand.set_target_position(j, 5000)
    hand.set_position_velocity(j, 15000)
hand.move_motors(0)
```
