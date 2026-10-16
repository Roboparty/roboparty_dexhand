#!/usr/bin/env python3
"""Publish two thumb sweeps (2 s each), then a four-finger wave (6 s), at 50 Hz."""
import argparse
import time

import rclpy
from rclpy.duration import Duration
from rclpy.executors import ExternalShutdownException
from std_msgs.msg import Int32MultiArray
from wave_motion import THUMB_SECONDS, WAVE_SECONDS, target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=['left', 'right'], default='left')
    parser.add_argument('--amplitude', type=int, default=6000)
    args = parser.parse_args()
    if not 1 <= args.amplitude <= 10000:
        parser.error('amplitude must be in 1..10000')
    rclpy.init()
    node = rclpy.create_node('rphand_wave', namespace='dexhand_' + args.side)
    publisher = node.create_publisher(Int32MultiArray, 'cmd', 1)
    try:
        deadline = time.monotonic() + 3
        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.02)
            if publisher.get_subscription_count():
                break
        else:
            raise SystemExit('No cmd subscriber found.')
        start = next_tick = time.monotonic()
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0)
            seconds = time.monotonic() - start
            if seconds >= 2 * THUMB_SECONDS + WAVE_SECONDS:
                publisher.publish(Int32MultiArray(data=[0] * 6))
                publisher.wait_for_all_acked(Duration(seconds=1))
                break
            publisher.publish(Int32MultiArray(data=target(seconds, args.amplitude)))
            next_tick = max(next_tick + 0.02, time.monotonic())
            time.sleep(max(0.0, next_tick - time.monotonic()))
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
