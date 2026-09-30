#!/usr/bin/env python3
"""双手波浪示例：会使能、回零并运动，运行前清空运动空间。"""
from concurrent.futures import ThreadPoolExecutor, wait
import argparse
import math
import signal
import threading
import time

def target(seconds, direction, options):
    # 六轴：拇指侧摆、拇指屈伸、食指、中指、无名指、小指。
    duration = options.period_ms / 1000 * options.cycles
    edge = max(0.0, min(1.0, min(seconds, duration - seconds) / min(2.0, duration / 4)))
    amplitude = options.amplitude * (0.5 - 0.5 * math.cos(math.pi * edge))
    phase = 2 * math.pi * seconds / (options.period_ms / 1000)
    return [0, 0] + [math.floor(amplitude * (1 - math.cos(phase + direction * j * math.pi / 2)) / 2 + 0.5)
                     for j in range(4)]  # 拇指保持零位，左右四指波浪方向相反。


def checked_state(hand, side):
    state = hand.get_state()
    if not (state.feedback_valid and all(state.enabled) and all(state.homed) and not any(state.fault_codes)):
        raise RuntimeError(f"{side}反馈异常：valid={state.feedback_valid}, enabled={state.enabled}, "
                           f"homed={state.homed}, faults={state.fault_codes}")
    return state


def print_state(side, state):
    print(f"{side} 状态：位置={state.positions}, 速度={state.velocities}, "
          f"电流={getattr(state, 'currents', ())}, 使能={state.enabled}, 回零={state.homed}, "
          f"力矩到位={getattr(state, 'torque_reached', ())}, 报警={state.fault_codes}, "
          f"反馈有效={state.feedback_valid}", flush=True)


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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rate', type=int, choices=[50, 100, 150, 200], default=200)
    parser.add_argument('--left', nargs=2, default=['can0', '1'], metavar=('CAN', 'NODE'))
    parser.add_argument('--right', nargs=2, default=['can3', '1'], metavar=('CAN', 'NODE'))
    parser.add_argument('--speed', type=int, default=20000)
    parser.add_argument('--current', type=int, default=400)
    parser.add_argument('--amplitude', type=int, default=6000)
    parser.add_argument('--period-ms', type=int, default=3000)
    parser.add_argument('--cycles', type=int, default=10)
    parser.add_argument('--dry-run', action='store_true', help='Check targets without loading SDK or opening CAN')
    options = parser.parse_args()
    for name, low, high in [('speed', 1, 20000), ('current', 0, 3000),
                            ('amplitude', 1, 10000), ('period_ms', 1000, 60000), ('cycles', 1, 100)]:
        if not low <= getattr(options, name) <= high:
            parser.error(f'{name} must be in {low}..{high}')
    for device in (options.left, options.right):
        try:
            device[1] = int(device[1])
        except ValueError:
            parser.error('node ID must be an integer')
        if not device[0] or not 1 <= device[1] <= 127:
            parser.error('device requires a CAN interface and node ID in 1..127')
    if options.left == options.right:
        parser.error('left and right must be different devices')
    duration = options.period_ms / 1000 * options.cycles
    if options.dry_run:
        for tick in range(int(duration * options.rate) + 1):
            seconds = tick / options.rate
            values = [target(seconds, direction, options) for direction in (-1, 1)]
            for positions in values:
                if positions[:2] != [0, 0] or not all(0 <= v <= options.amplitude for v in positions):
                    raise RuntimeError('Invalid generated target')
            if tick % options.rate == 0:
                print(f't={seconds:g}s left={values[0]} right={values[1]}')
        print('Dry run passed; no CAN connection or hardware commands.')
        return 0
    from roboparty_dexhand_sdk import Hand

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
        parallel("连接", lambda: handleft.connect(*options.left), lambda: handright.connect(*options.right))
        parallel("设置速度", lambda: handleft.set_speed(options.speed), lambda: handright.set_speed(options.speed))
        parallel("设置限流", lambda: handleft.set_current_limit(options.current), lambda: handright.set_current_limit(options.current))
        parallel("力矩到位停止", lambda: handleft.set_torque_reached_mode(True), lambda: handright.set_torque_reached_mode(True))
        parallel("使能", handleft.enable, handright.enable)
        parallel("回零", handleft.home, handright.home)
        parallel("食指目标归零", lambda: handleft.set_position(3, 0), lambda: handright.set_position(3, 0))

        print(f"双手波浪：{options.rate} Hz 目标更新", flush=True)
        start = next_tick = time.monotonic()
        next_report = start
        while (seconds := time.monotonic() - start) < duration:
            if interrupted.is_set():
                raise KeyboardInterrupt
            for (hand, side), direction in zip(hands, (-1, 1)):
                state = checked_state(hand, side)
                if not hand.set_positions(target(seconds, direction, options)):
                    raise RuntimeError(f"{side}波浪目标下发失败")
                if time.monotonic() >= next_report:
                    print_state(side, state)
                    if direction == 1:
                        next_report += 1.0
            next_tick = max(next_tick + 1 / options.rate, time.monotonic())
            interrupted.wait(max(0.0, next_tick - time.monotonic()))

        # 下发成功不等于到位；结合位置、速度和有效状态确认双手回到零位。
        parallel("返回零位", lambda: handleft.set_positions([0] * 6), lambda: handright.set_positions([0] * 6))
        deadline = time.monotonic() + 3.0 + options.amplitude / options.speed
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
    raise SystemExit(main())
