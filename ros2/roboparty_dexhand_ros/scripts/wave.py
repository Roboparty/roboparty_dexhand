#!/usr/bin/env python3
"""Publish a single-hand four-finger wave at 50 Hz for 6 seconds."""
import argparse
import math
import time

import rclpy
from rclpy.duration import Duration
from rclpy.executors import ExternalShutdownException
from roboparty_dexhand_ros.msg import DexHandState
from std_msgs.msg import Int32MultiArray


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=['left', 'right'], default='left')
    parser.add_argument('--amplitude', type=int, default=6000)
    args = parser.parse_args()
    if not 1 <= args.amplitude <= 10000:
        parser.error('amplitude must be in 1..10000')
    rclpy.init()
    node = rclpy.create_node('dexhand_wave', namespace='dexhand_' + args.side)
    publisher = node.create_publisher(Int32MultiArray, 'cmd', 1)
    latest = [None, 0.0]

    def on_state(msg):
        latest[:] = [msg, time.monotonic()]

    node.create_subscription(DexHandState, 'state', on_state, 1)

    def ready():
        state, received = latest
        return (state is not None and time.monotonic() - received < 0.2
                and state.phase == DexHandState.READY and state.feedback_valid)

    try:
        deadline = time.monotonic() + 3
        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.02)
            if ready() and publisher.get_subscription_count():
                break
        else:
            raise SystemExit('No fresh READY state; reset the hand first.')
        start = next_tick = time.monotonic()
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0)
            if not ready():
                raise SystemExit('READY feedback lost; wave publisher exited.')
            seconds = time.monotonic() - start
            if seconds >= 6:
                publisher.publish(Int32MultiArray(data=[0] * 6))
                publisher.wait_for_all_acked(Duration(seconds=1))
                break
            edge = max(0.0, min(1.0, seconds, 6 - seconds))
            amplitude = args.amplitude * (1 - math.cos(math.pi * edge)) / 2
            positions = [0, 0] + [round(amplitude * (1 - math.cos(
                2 * math.pi * seconds + j * math.pi / 2)) / 2) for j in range(4)]
            publisher.publish(Int32MultiArray(data=positions))
            next_tick = max(next_tick + 0.02, time.monotonic())
            time.sleep(max(0.0, next_tick - time.monotonic()))
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
