"""Verify dual-hand sequencing, feedback and cancellation without hardware."""
import contextlib
import importlib.util
import io
import itertools
from pathlib import Path
import threading
import types
import unittest
from unittest.mock import Mock, call, patch

spec = importlib.util.spec_from_file_location(
    "wave_demo", Path(__file__).resolve().parents[1] / "examples/wave_demo.py")
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


class WaveDemoTests(unittest.TestCase):
    def setUp(self):
        self.left, self.right = Mock(), Mock()
        self.interrupted = threading.Event()
        self.interrupted.wait = Mock(side_effect=lambda timeout: self.interrupted.is_set())
        for hand in (self.left, self.right):
            hand.get_state.return_value = types.SimpleNamespace(
                feedback_valid=True, enabled=(True,) * 6, homed=(True,) * 6,
                fault_codes=(0,) * 6, positions=(0,) * 6, velocities=(0,) * 6)

    def run_demo(self):
        # Patch only event construction in the demo; executors still use real threading events.
        fake_threading = types.SimpleNamespace(Event=lambda: self.interrupted)
        with patch.object(demo, "Hand", side_effect=[self.left, self.right]), \
                patch.object(demo, "threading", fake_threading), \
                patch.object(demo, "DURATION", 0.1), \
                patch.object(demo.time, "monotonic", side_effect=itertools.count(0, 0.005)), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            code = demo.main()
        self.output = output.getvalue()
        return code

    def assert_cleanup(self):
        for hand in (self.left, self.right):
            hand.stop.assert_not_called()
            self.assertEqual(hand.method_calls[-2:], [call.disable(), call.disconnect()])

    def test_cpp_sequence_and_parallel_home(self):
        barrier = threading.Barrier(2)
        for hand in (self.left, self.right):
            hand.home.side_effect = lambda: (barrier.wait(timeout=2), True)[1]
        self.assertEqual(self.run_demo(), 0)
        self.left.connect.assert_called_once_with("can0", 1)
        self.right.connect.assert_called_once_with("can3", 1)
        for hand in (self.left, self.right):
            hand.assert_has_calls([call.set_speed(20000), call.set_current_limit(400),
                                   call.set_torque_reached_mode(True), call.enable(),
                                   call.home(), call.set_position(3, 0)])
            self.assertGreater(hand.set_positions.call_count, 1)
            self.assertEqual(hand.set_positions.call_args, call([0] * 6))
        self.assert_cleanup()

    def test_cpp_targets(self):
        self.assertEqual((demo.AMPLITUDE, demo.PERIOD, demo.CYCLES), (6000, 3.0, 10))
        self.assertEqual(demo.target(1, -1), [0, 0, 2250, 201, 750, 2799])
        self.assertEqual(demo.target(1, 1), [0, 0, 2250, 2799, 750, 201])
        self.assertEqual(demo.target(2, -1), [0, 0, 4500, 5598, 1500, 402])
        for direction in (-1, 1):
            self.assertEqual(demo.target(0, direction), [0] * 6)
            self.assertEqual(demo.target(demo.DURATION, direction), [0] * 6)

    def test_failure_does_not_skip_other_hand(self):
        self.left.enable.return_value = False
        self.assertEqual(self.run_demo(), 1)
        self.right.enable.assert_called_once()
        self.left.home.assert_not_called()
        self.assertIn("左手=False", self.output)
        self.assert_cleanup()

    def test_failed_connection_only_disconnects(self):
        self.right.connect.return_value = False
        self.right.is_connected.return_value = False
        self.assertEqual(self.run_demo(), 1)
        self.right.stop.assert_not_called()
        self.right.disable.assert_not_called()
        self.right.disconnect.assert_called_once()

    def test_fault_or_stale_feedback_prevents_right_motion(self):
        for attribute, value in (("feedback_valid", False), ("fault_codes", (1, 0, 0, 0, 0, 0))):
            with self.subTest(attribute=attribute):
                self.setUp()
                setattr(self.right.get_state.return_value, attribute, value)
                self.assertEqual(self.run_demo(), 1)
                self.right.set_positions.assert_not_called()
                self.assertIn("右手反馈异常", self.output)
                self.assert_cleanup()

    def test_return_timeout(self):
        self.right.get_state.return_value.positions = (1000,) * 6
        self.assertEqual(self.run_demo(), 1)
        self.assertIn("返回零位超时", self.output)
        self.assert_cleanup()

    def test_disable_failure_still_disconnects(self):
        self.left.disable.side_effect = RuntimeError("disable failed")
        self.assertEqual(self.run_demo(), 1)
        self.assert_cleanup()

    def test_interrupt_cancels_blocking_home_before_join(self):
        barrier = threading.Barrier(2)
        released = threading.Event()

        def home():
            barrier.wait(timeout=2)
            self.interrupted.set()
            if not released.wait(2):
                raise RuntimeError("cleanup did not cancel home")
            return False

        for hand in (self.left, self.right):
            hand.home.side_effect = home
            hand.disable.side_effect = lambda: (released.set(), True)[1]
        self.assertEqual(self.run_demo(), 130)
        self.left.set_positions.assert_not_called()
        self.right.set_positions.assert_not_called()
        self.assertTrue(released.is_set())
        self.assert_cleanup()


if __name__ == "__main__":
    unittest.main()
