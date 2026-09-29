#!/usr/bin/env python3
"""双手波浪示例：会使能、回零并运动，运行前清空运动空间。"""
from concurrent.futures import ThreadPoolExecutor, wait
import math
import signal
import sys
import threading
import time

from roboparty_dexhand_sdk import Hand

SPEED, CURRENT = 20000, 400  # 行程当量/秒；厂家限流千分比，不是 mA。
AMPLITUDE, PERIOD, CYCLES = 6000, 3.0, 10  # 与 C++ 默认参数一致。
DURATION = PERIOD * CYCLES


def target(seconds, direction):
    # 六轴：拇指侧摆、拇指屈伸、食指、中指、无名指、小指。
    edge = max(0.0, min(1.0, min(seconds, DURATION - seconds) / min(2.0, DURATION / 4)))
    amplitude = AMPLITUDE * (0.5 - 0.5 * math.cos(math.pi * edge))
    phase = 2 * math.pi * seconds / PERIOD
    return [0, 0] + [math.floor(amplitude * (1 - math.cos(phase + direction * j * math.pi / 2)) / 2 + 0.5)
                     for j in range(4)]  # 拇指保持零位，左右四指波浪方向相反。


def checked_state(hand, side):
    state = hand.get_state()
    if not (state.feedback_valid and all(state.enabled) and all(state.homed) and not any(state.fault_codes)):
        raise RuntimeError(f"{side}反馈异常：valid={state.feedback_valid}, enabled={state.enabled}, "
                           f"homed={state.homed}, faults={state.fault_codes}")
    return state


def close_hand(hand, side):
    ok = True
    # 失能失败仍继续断开；未连接的手只断开。
    actions = (hand.disable, hand.disconnect) if hand.is_connected() else (hand.disconnect,)
    for action in actions:
        try:
            if action() is False:
                raise RuntimeError(f"{action.__name__} 未确认")
        except Exception as error:
            print(f"{side}清理失败：{error}", flush=True)
            ok = False
    return ok


def main():
    handleft, handright = Hand(), Hand()
    hands = ((handleft, "左手"), (handright, "右手"))
    interrupted = threading.Event()
    previous = {sig: signal.signal(sig, lambda *_: interrupted.set()) for sig in (signal.SIGINT, signal.SIGTERM)}
    pool = ThreadPoolExecutor(max_workers=2)
    exit_code = 0

    def parallel(name, left_call, right_call):
        # 双手同时执行，主线程每 20 ms 检查中断，避免回零时无法清理。
        if interrupted.is_set():
            raise KeyboardInterrupt
        print(f"{name}...", flush=True)
        futures = [pool.submit(left_call), pool.submit(right_call)]
        while wait(futures, timeout=0.02)[1]:
            if interrupted.is_set():
                raise KeyboardInterrupt
        if interrupted.is_set():
            raise KeyboardInterrupt
        left_ok, right_ok = [future.result() for future in futures]
        if not (left_ok and right_ok):
            raise RuntimeError(f"{name}失败：左手={left_ok}，右手={right_ok}")

    try:
        # 按实际接线修改接口名和节点；每阶段内部最多尝试 30 次。
        parallel("连接", lambda: handleft.connect("can0", 1), lambda: handright.connect("can3", 1))
        parallel("设置速度", lambda: handleft.set_speed(SPEED), lambda: handright.set_speed(SPEED))
        parallel("设置限流", lambda: handleft.set_current_limit(CURRENT), lambda: handright.set_current_limit(CURRENT))
        parallel("力矩到位停止", lambda: handleft.set_torque_reached_mode(True), lambda: handright.set_torque_reached_mode(True))
        parallel("使能", handleft.enable, handright.enable)
        parallel("回零", handleft.home, handright.home)
        parallel("食指目标归零", lambda: handleft.set_position(3, 0), lambda: handright.set_position(3, 0))

        print("双手波浪：200 Hz 目标更新", flush=True)
        start = next_tick = time.monotonic()
        count = 0
        while (seconds := time.monotonic() - start) < DURATION:
            if interrupted.is_set():
                raise KeyboardInterrupt
            for (hand, side), direction in zip(hands, (-1, 1)):
                state = checked_state(hand, side)
                if not hand.set_positions(target(seconds, direction)):
                    raise RuntimeError(f"{side}波浪目标下发失败")
                if count % 200 == 0:
                    print(f"{side}实际位置：{state.positions}", flush=True)
            count += 1
            next_tick = max(next_tick + 0.005, time.monotonic())  # 与 C++ 一样扣除下发耗时。
            interrupted.wait(max(0.0, next_tick - time.monotonic()))

        # 下发成功不等于到位；结合位置、速度和有效状态确认双手回到零位。
        parallel("返回零位", lambda: handleft.set_positions([0] * 6), lambda: handright.set_positions([0] * 6))
        deadline = time.monotonic() + 3.0 + AMPLITUDE / SPEED
        while time.monotonic() < deadline:
            if interrupted.is_set():
                raise KeyboardInterrupt
            states = [checked_state(hand, side) for hand, side in hands]
            if all(all(abs(p) <= 150 for p in s.positions) and all(abs(v) <= 50 for v in s.velocities) for s in states):
                break
            interrupted.wait(0.02)
        else:
            raise RuntimeError("返回零位超时")
    except KeyboardInterrupt:
        interrupted.set()
        exit_code = 130
    except Exception as error:
        print(error, flush=True)
        exit_code = 1
    finally:
        # 先取消底层等待，再回收应用线程；双手并行清理，避免一只手等待另一只。
        pool.shutdown(wait=False, cancel_futures=True)
        try:
            print("失能 -> 断开双手", flush=True)
            with ThreadPoolExecutor(max_workers=2) as cleanup_pool:
                results = list(cleanup_pool.map(lambda item: close_hand(*item), hands))
            if not all(results):
                exit_code = exit_code or 1
        finally:
            pool.shutdown(wait=True)
            for sig, handler in previous.items():
                signal.signal(sig, handler)
    return 130 if interrupted.is_set() else exit_code


if __name__ == "__main__":
    if len(sys.argv) != 1:
        raise SystemExit("此示例不接受命令行参数；请修改文件中的设备与波浪参数。")
    raise SystemExit(main())
