#!/usr/bin/env python3
"""拇指侧摆往返、拇指屈伸往返、四指 wave，按时间连续循环到 Ctrl+C。"""
from concurrent.futures import ThreadPoolExecutor, wait
import argparse
import math
import signal
import threading
import time

RATE = 50
THUMB_SECONDS = 2
WAVE_SECONDS = 6
ROUND_SECONDS = 2 * THUMB_SECONDS + WAVE_SECONDS


def target(seconds, amplitude):
    t = seconds % ROUND_SECONDS
    values = [0] * 6
    if t < 2 * THUMB_SECONDS:
        axis = 0 if t < THUMB_SECONDS else 1
        phase = 2 * math.pi * (t - axis * THUMB_SECONDS) / THUMB_SECONDS
        values[axis] = math.floor(5000 * (1 - math.cos(phase)) + 0.5)
    else:
        local = t - 2 * THUMB_SECONDS
        edge = max(0.0, min(1.0, local, WAVE_SECONDS - local))
        envelope = (1 - math.cos(math.pi * edge)) / 2
        values[2:] = [math.floor(amplitude * envelope *
                     (1 - math.cos(2 * math.pi * local + j * math.pi / 2)) / 2 + 0.5)
                     for j in range(4)]
    return values


def close_hand(hand):
    ok = True
    try:
        if hand.is_connected() and not hand.disable():
            print("失能未确认", flush=True)
            ok = False
    except Exception as error:
        print(f"失能失败：{error}", flush=True)
        ok = False
    finally:
        hand.disconnect()
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=['left', 'right'], default='left')
    parser.add_argument('--amplitude', type=int, default=6000, help='Four-finger amplitude (1..10000, default: 6000)')
    parser.add_argument('--dry-run', action='store_true', help='Preview one round without SDK or CAN')
    options = parser.parse_args()
    if not 1 <= options.amplitude <= 10000:
        parser.error('amplitude must be in 1..10000')
    if options.dry_run:
        for tick in range(ROUND_SECONDS * RATE + 1):
            values = target(tick / RATE, options.amplitude)
            if any(v < 0 or v > (10000 if axis < 2 else options.amplitude) for axis, v in enumerate(values)):
                raise RuntimeError('Invalid generated target')
            if tick % 5 == 0:
                print(f'tick={tick} positions:', *values)
        print('Dry run passed; no CAN connection or hardware commands.')
        return 0

    from roboparty_dexhand_sdk import Hand
    hand = Hand()
    interrupted = threading.Event()
    previous = {sig: signal.signal(sig, lambda *_: interrupted.set()) for sig in (signal.SIGINT, signal.SIGTERM)}
    pool = ThreadPoolExecutor(max_workers=1)
    result = 0

    def initialize():
        steps = [('连接', lambda: hand.connect('can0' if options.side == 'left' else 'can3', 1)),
                 ('失能', hand.disable), ('设置速度', lambda: hand.set_speed(16000)),
                 ('设置限流', lambda: hand.set_current_limit(400)),
                 ('力矩到位模式', lambda: hand.set_torque_reached_mode(False)),
                 ('使能', hand.enable), ('回零', hand.home)]
        for name, call in steps:
            if interrupted.is_set():
                return
            print(f'{name}...', flush=True)
            if not call():
                raise RuntimeError(f'{name}失败')

    try:
        initialization = pool.submit(initialize)
        while wait([initialization], timeout=0.02)[1]:
            if interrupted.is_set():
                raise KeyboardInterrupt
        initialization.result()
        print('拇指侧摆 -> 拇指屈伸 -> 四指 wave，50 Hz 连续循环；Ctrl+C 退出。', flush=True)
        start = next_tick = time.monotonic()
        while not interrupted.is_set():
            state = hand.get_state()
            if not (state.feedback_valid and all(state.enabled) and all(state.homed) and not any(state.fault_codes)):
                raise RuntimeError('反馈无效、失能、未回零或存在故障')
            if not hand.set_positions(target(time.monotonic() - start, options.amplitude)):
                raise RuntimeError('位置目标下发失败')
            next_tick = max(next_tick + 1 / RATE, time.monotonic())
            interrupted.wait(max(0.0, next_tick - time.monotonic()))
    except KeyboardInterrupt:
        interrupted.set()
    except Exception as error:
        print(error, flush=True)
        result = 1
    finally:
        # Cancel pending SDK work before joining the initialization thread.
        if interrupted.is_set():
            result = 130
        interrupted.set()
        print('失能 -> 断开', flush=True)
        try:
            if not close_hand(hand):
                result = 1
        finally:
            pool.shutdown(wait=True)
            for sig, handler in previous.items():
                signal.signal(sig, handler)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
