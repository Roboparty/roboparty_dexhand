#!/usr/bin/env python3
"""Map D-pad axes to low-frequency dexhand reset and wave actions."""

import math
import time

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from sensor_msgs.msg import Joy
from std_msgs.msg import Int32MultiArray
from std_srvs.srv import Trigger
from wave_motion import THUMB_SECONDS, WAVE_SECONDS, target

class DexHandJoyMapper(Node):
    """Use axes 6/7 D-pad edges to trigger one safe hand action at a time."""

    def __init__(self) -> None:
        super().__init__("rphand_joy_mapper")
        self.declare_parameter("horizontal_axis", 6)
        self.declare_parameter("vertical_axis", 7)
        self.declare_parameter("motor_enable_button", 2)
        self.declare_parameter("threshold", 0.5)
        self.declare_parameter("wave_amplitude", 6000)
        self.declare_parameter("wave_duration", WAVE_SECONDS)

        self._horizontal_axis = int(self.get_parameter("horizontal_axis").value)
        self._vertical_axis = int(self.get_parameter("vertical_axis").value)
        self._motor_enable_button = int(self.get_parameter("motor_enable_button").value)
        self._threshold = float(self.get_parameter("threshold").value)
        self._wave_amplitude = int(self.get_parameter("wave_amplitude").value)
        self._wave_duration = float(self.get_parameter("wave_duration").value)
        if not 1 <= self._wave_amplitude <= 10000:
            raise ValueError("wave_amplitude must be in 1..10000")
        if not math.isfinite(self._wave_duration) or self._wave_duration <= 0:
            raise ValueError("wave_duration must be positive and finite")

        self._direction = ""
        self._robot_enabled = False
        self._last_motor_button = 0
        self._reset_pending = {"left": False, "right": False}
        self._wave_started = {"left": None, "right": None}

        self._cmd_publishers = {
            side: self.create_publisher(
                Int32MultiArray, f"/dexhand_{side}/cmd", 1)
            for side in ("left", "right")
        }
        self._reset_clients = {
            side: self.create_client(Trigger, f"/dexhand_{side}/reset")
            for side in ("left", "right")
        }
        self.create_subscription(Joy, "/joy", self._on_joy, 10)
        self.create_timer(0.02, self._wave_tick)
        self.get_logger().info(
            f"ready: axes[{self._horizontal_axis}]/axes[{self._vertical_axis}], "
            f"button[{self._motor_enable_button}]=motor enable, "
            "up=left reset, down=right reset, left=left wave, right=right wave")

    def _axis(self, msg: Joy, index: int) -> float:
        return float(msg.axes[index]) if index < len(msg.axes) else 0.0

    def _read_direction(self, msg: Joy) -> str:
        horizontal = self._axis(msg, self._horizontal_axis)
        vertical = self._axis(msg, self._vertical_axis)
        # This controller reports the D-pad with the opposite sign from the
        # usual SDL convention: physical up/left are positive values.
        if vertical > self._threshold:
            return "up"
        if vertical < -self._threshold:
            return "down"
        if horizontal > self._threshold:
            return "left"
        if horizontal < -self._threshold:
            return "right"
        return ""

    def _on_joy(self, msg: Joy) -> None:
        self._handle_motor_button(msg)
        direction = self._read_direction(msg)
        if not direction or direction == self._direction:
            self._direction = direction
            return
        self._direction = direction
        actions = {
            "up": ("left", "reset"),
            "down": ("right", "reset"),
            "left": ("left", "wave"),
            "right": ("right", "wave"),
        }
        side, action = actions[direction]
        if action == "reset":
            self._reset(side)
        else:
            self._start_wave(side)

    def _handle_motor_button(self, msg: Joy) -> None:
        """Mirror the RP1 motor toggle and reset both hands on enable."""
        button = (1 if self._motor_enable_button < len(msg.buttons)
                  and msg.buttons[self._motor_enable_button] else 0)
        rising = button == 1 and self._last_motor_button == 0
        self._last_motor_button = button
        if not rising:
            return
        if self._robot_enabled:
            self._robot_enabled = False
            self.get_logger().info("motor toggle -> disabled; hands unchanged")
            return
        self._robot_enabled = True
        self.get_logger().info("motor toggle -> enabled; resetting both hands")
        self._reset_both_hands()

    def _reset(self, side: str) -> None:
        if self._reset_pending[side]:
            self.get_logger().info(f"{side} reset request pending")
            return
        self._reset_pending[side] = True
        self.get_logger().info(f"{'UP' if side == 'left' else 'DOWN'} -> {side} reset")
        client = self._reset_clients[side]
        if not client.service_is_ready():
            self.get_logger().warning(f"{side} reset ignored: service is not ready")
            self._reset_pending[side] = False
            return
        future = client.call_async(Trigger.Request())
        future.add_done_callback(lambda done, current_side=side: self._reset_done(done, current_side))

    def _reset_both_hands(self) -> None:
        """Submit restart signals independently of topic playback."""
        for side in ("left", "right"):
            self._reset(side)

    def _reset_done(self, future, side: str) -> None:
        try:
            response = future.result()
            log = self.get_logger().info if response.success else self.get_logger().error
            log(f"{side} reset: {response.message}")
        except Exception as exc:  # noqa: BLE001
            self.get_logger().error(f"{side} reset failed: {exc}")
        finally:
            self._reset_pending[side] = False

    def _start_wave(self, side: str) -> None:
        self._wave_started[side] = time.monotonic()
        self.get_logger().info(f"{'LEFT' if side == 'left' else 'RIGHT'} -> {side} wave")

    def _wave_tick(self) -> None:
        for side in ("left", "right"):
            started = self._wave_started[side]
            if started is None:
                continue
            elapsed = time.monotonic() - started
            if elapsed >= 2 * THUMB_SECONDS + self._wave_duration:
                self._cmd_publishers[side].publish(Int32MultiArray(data=[0] * 6))
                self.get_logger().info(f"{side} wave finished")
                self._wave_started[side] = None
                continue
            positions = target(elapsed, self._wave_amplitude, self._wave_duration)
            self._cmd_publishers[side].publish(Int32MultiArray(data=positions))


def main() -> None:
    rclpy.init()
    node = DexHandJoyMapper()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
