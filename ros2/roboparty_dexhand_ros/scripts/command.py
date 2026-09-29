#!/usr/bin/env python3
"""Send one six-axis target after observing fresh READY feedback."""
import argparse
import time

import rclpy
from rclpy.qos import QoSProfile
from roboparty_dexhand_ros.msg import DexHandState
from std_msgs.msg import Int32MultiArray


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=['left', 'right'], default='left')
    parser.add_argument('positions', nargs=6, type=int)
    args = parser.parse_args()
    if any(value < 0 or value > 10000 for value in args.positions):
        parser.error('positions must be in 0..10000')
    rclpy.init()
    node = rclpy.create_node('dexhand_command', namespace='dexhand_' + args.side)
    latest = [None, 0.0]

    def on_state(msg):
        latest[:] = [msg, time.monotonic()]

    subscription = node.create_subscription(DexHandState, 'state', on_state, QoSProfile(depth=1))
    publisher = node.create_publisher(Int32MultiArray, 'cmd', QoSProfile(depth=1))
    try:
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
            state, received = latest
            if (publisher.get_subscription_count() and state is not None
                    and time.monotonic() - received < 0.1
                    and state.phase == DexHandState.READY and state.feedback_valid):
                publisher.publish(Int32MultiArray(data=args.positions))
                publisher.wait_for_all_acked(rclpy.duration.Duration(seconds=1))
                node.get_logger().info('Target sent; driver repeats it at 200 Hz until stop/reset/error.')
                return
        raise SystemExit('No fresh READY state: call reset and wait for homing to finish first.')
    finally:
        node.destroy_subscription(subscription)
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
