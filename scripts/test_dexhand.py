#!/usr/bin/env python3
"""roboparty_dexhand 测试脚本。

用 dexhand_py 库控制灵巧手开合。
这是迁移到工厂模式后的第一次测试, 验证 dexhand_py 能正常工作。

用法:
    cd /home/sjh/leisai_hand/roboparty_dexhand
    LD_LIBRARY_PATH=thirdparty/lib python3 scripts/test_dexhand.py

注意: 需要用系统python3.12(不是conda python), 因为dexhand_py是为3.12编译的。
"""

import sys
import time
from pathlib import Path

# 确保能找到 dexhand_py 模块
BUILD_DIR = Path(__file__).resolve().parent.parent / "build"
sys.path.insert(0, str(BUILD_DIR))

# 确保能找到 libLHandProLib.so
# (也可以用 LD_LIBRARY_PATH 环境变量)
import ctypes
so_path = Path(__file__).resolve().parent.parent / "thirdparty" / "lib" / "libLHandProLib.so"
try:
    ctypes.CDLL(str(so_path))
    print(f"✅ libLHandProLib.so 加载成功")
except OSError as e:
    print(f"❌ 无法加载 libLHandProLib.so: {e}")
    sys.exit(1)

from dexhand_py import HandDriver, HandModel


def main():
    print("=" * 60)
    print("  roboparty_dexhand 灵巧手测试")
    print("=" * 60)

    # 1. 创建灵巧手驱动(工厂模式)
    print("\n[1] 创建灵巧手驱动...")
    hand = HandDriver.create_hand(
        hand_type="LHandPro",
        interface_type="canfd",
        interface="can0",
        canfd_node_id=1)
    print(f"  驱动创建成功: {hand}")

    # 2. 初始化(连接+使能+回零)
    print("\n[2] 初始化灵巧手(连接+使能+回零, 约7秒)...")
    ok = hand.init_hand(
        enable_motors=True,
        home_motors=True,
        home_wait_time=6.0)
    print(f"  初始化: {'✅ 成功' if ok else '❌ 失败'}")

    if not ok:
        print("初始化失败, 退出")
        return 1

    # 读DOF
    total, active = hand.get_dof()
    print(f"  DOF: total={total}, active={active}")

    # 3. 开合测试
    print("\n[3] 开合测试(3轮)...")
    for i in range(3):
        # 握紧
        print(f"  [{i+1}/3] 握紧...")
        for j in range(1, active + 1):
            hand.set_target_position(j, 5000)
            hand.set_position_velocity(j, 15000)
        hand.move_motors(0)  # 0=广播所有手指
        time.sleep(2.0)

        # 读当前位置
        pos1 = hand.get_now_position(1)  # 第1个手指的位置
        print(f"    手指1位置: {pos1}")

        # 张开
        print(f"  [{i+1}/3] 张开...")
        for j in range(1, active + 1):
            hand.set_target_position(j, 0)
            hand.set_position_velocity(j, 15000)
        hand.move_motors(0)
        time.sleep(2.0)

        pos2 = hand.get_now_position(1)
        print(f"    手指1位置: {pos2}")

    # 4. 收尾
    print("\n[4] 收尾...")
    # 张开
    for j in range(1, active + 1):
        hand.set_target_position(j, 0)
        hand.set_position_velocity(j, 15000)
    hand.move_motors(0)
    time.sleep(1.0)

    hand.deinit_hand()
    print("  灵巧手已断开")
    print("\n测试完成!")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
