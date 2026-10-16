#!/usr/bin/env python3
"""Publish one six-axis target; initialize the hand with reset beforehand."""
import argparse
import time

import rclpy
from rclpy.qos import QoSProfile
from std_msgs.msg import Int32MultiArray


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=['left', 'right'], default='left')
    parser.add_argument('positions', nargs=6, type=int)
    args = parser.parse_args()
    if any(value < 0 or value > 10000 for value in args.positions):
        parser.error('positions must be in 0..10000')
    rclpy.init()
    node = rclpy.create_node('rphand_command', namespace='dexhand_' + args.side)
    publisher = node.create_publisher(Int32MultiArray, 'cmd', QoSProfile(depth=1))
    try:
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
            if publisher.get_subscription_count():
                publisher.publish(Int32MultiArray(data=args.positions))
                publisher.wait_for_all_acked(rclpy.duration.Duration(seconds=1))
                node.get_logger().info('Target sent; driver repeats it until updated, reset or shutdown.')
                return
        raise SystemExit('No cmd subscriber found.')
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
