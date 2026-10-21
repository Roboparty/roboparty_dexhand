#!/usr/bin/env python3
"""Repeat thumb 1, thumb 2 and four-finger wave until Ctrl+C."""
from concurrent.futures import ThreadPoolExecutor, wait
import math
import signal
import threading
import time
from connection import connection_options, parse_options

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
            print("Disable unconfirmed.", flush=True)
            ok = False
    except Exception as error:
        print(f"Disable failed: {error}", flush=True)
        ok = False
    finally:
        hand.disconnect()
    return ok


def main():
    options = parse_options()
    from rphand import Hand
    hand = Hand()
    connection = connection_options(options)
    connect = lambda: hand.connect(connection)
    interrupted = threading.Event()
    previous = {sig: signal.signal(sig, lambda *_: interrupted.set()) for sig in (signal.SIGINT, signal.SIGTERM)}
    pool = ThreadPoolExecutor(max_workers=1)
    result = 0

    def initialize():
        steps = [('Connect', connect)]
        if not options.connect_only:
            steps.extend([('Disable', hand.disable), ('Set speed', lambda: hand.set_speed(16000)),
                          ('Set current limit', lambda: hand.set_current_limit(400)),
                          ('Set torque mode', lambda: hand.set_torque_reached_mode(False)),
                          ('Enable', hand.enable), ('Home', hand.home)])
        for name, call in steps:
            if interrupted.is_set():
                return
            print(f'{name}...', flush=True)
            if not call():
                raise RuntimeError(f'{name} failed')

    try:
        initialization = pool.submit(initialize)
        while wait([initialization], timeout=0.02)[1]:
            if interrupted.is_set():
                raise KeyboardInterrupt
        initialization.result()
        if options.connect_only:
            print('Checking feedback for 5 seconds...', flush=True)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if interrupted.is_set():
                    raise KeyboardInterrupt
                state = hand.get_state()
                if not state.feedback_valid or any(state.fault_codes):
                    raise RuntimeError('Invalid/fault feedback during connection check')
                interrupted.wait(0.02)
            print('Connection and feedback check passed.', flush=True)
        else:
            print('Repeating thumb 1 -> thumb 2 -> four-finger wave; Ctrl+C to exit.', flush=True)
        start = next_tick = time.monotonic()
        while not options.connect_only and not interrupted.is_set():
            state = hand.get_state()
            if not (state.feedback_valid and all(state.enabled) and all(state.homed) and not any(state.fault_codes)):
                raise RuntimeError('Invalid/disabled/unhomed/fault feedback')
            if not hand.set_positions(target(time.monotonic() - start, options.amplitude)):
                raise RuntimeError('Position send failed')
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
        print('Disable -> disconnect', flush=True)
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
