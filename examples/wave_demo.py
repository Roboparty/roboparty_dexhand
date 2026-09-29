#!/usr/bin/env python3
"""左右手波浪入门示例：会回零并运动双手，运行前清空运动空间。"""
import math
import sys
import time

from roboparty_dexhand_sdk import Hand

SPEED, CURRENT = 20000, 400  # 与 C++ demo 默认值一致。
AMPLITUDE, PERIOD, CYCLES = 6000, 3.0, 10  # 幅值 0..10000，周期单位为秒。
DURATION = PERIOD * CYCLES


def main():
    handleft = Hand()
    handright = Hand()
    exit_code = 0
    try:
        # 修改接口名和节点 ID，分别连接左右手。
        if not handleft.connect("can0", 1, retry_count=3):
            raise RuntimeError("左手连接失败")
        if not handright.connect("can3", 1, retry_count=3):
            raise RuntimeError("右手连接失败")
        for hand in (handleft, handright):
            # 速度为行程当量/秒，限流为厂家千分比，不是 mA。
            if not (hand.set_speed(SPEED) and hand.set_current_limit(CURRENT)
                    and hand.set_torque_reached_mode(True)):
                raise RuntimeError("参数设置失败")
        if not (handleft.enable() and handright.enable()):
            raise RuntimeError("使能失败")
        # 顺序等待双手回零；阻塞调用期间 Ctrl+C 可能延迟处理。
        if not (handleft.home() and handright.home()):
            raise RuntimeError("回零失败")

        start = time.monotonic()
        while (seconds := time.monotonic() - start) < DURATION:
            phase = 2 * math.pi * seconds / PERIOD
            ramp = min(2.0, DURATION / 4.0)
            edge = max(0.0, min(1.0, min(seconds, DURATION - seconds) / ramp))
            amplitude = AMPLITUDE * (0.5 - 0.5 * math.cos(math.pi * edge))  # 首尾余弦渐变。
            # 六轴：拇指侧摆、拇指屈伸、食指、中指、无名指、小指。
            # 两个拇指轴保持 0；四指相差 1/4 周期，左右手波浪方向相反。
            # 非负目标加 0.5 后取整，与 C++ std::lround 一致。
            left = [0, 0] + [math.floor(amplitude * (1 - math.cos(phase - j * math.pi / 2)) / 2 + 0.5) for j in range(4)]
            right = [0, 0] + [math.floor(amplitude * (1 - math.cos(phase + j * math.pi / 2)) / 2 + 0.5) for j in range(4)]
            if not (handleft.set_positions(left) and handright.set_positions(right)):
                raise RuntimeError("波浪目标下发失败")
            time.sleep(0.02)  # 约 50 Hz；下发成功不代表实际到位。
    except KeyboardInterrupt:
        exit_code = 130
    except Exception as error:
        print(error)
        exit_code = 1
    finally:
        # 停止失败仍尝试失能、断开；断开本身不保证物理停机。
        for hand in (handleft, handright):
            for action in (hand.stop, hand.disable, hand.disconnect):
                try:
                    if action() is False:
                        print("停止/失能未确认，请检查设备状态")
                        exit_code = exit_code or 1
                except Exception as error:
                    print(error)
                    exit_code = exit_code or 1
    return exit_code


if __name__ == "__main__":
    if len(sys.argv) != 1:
        raise SystemExit("此示例不接受命令行参数；请修改文件中的设备与波浪参数。")
    raise SystemExit(main())
