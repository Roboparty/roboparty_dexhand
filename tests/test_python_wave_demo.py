"""Check the short dual-hand example without hardware or real-time waits."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import Mock, call, patch

spec = importlib.util.spec_from_file_location(
    "wave_demo", Path(__file__).resolve().parents[1] / "examples/wave_demo.py")
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


class WaveDemoTests(unittest.TestCase):
    def setUp(self):
        self.left, self.right = Mock(), Mock()

    def run_demo(self):
        with patch.object(demo, "Hand", side_effect=[self.left, self.right]):
            with patch.object(demo.time, "monotonic", side_effect=[0, 1, 2, 30]):
                with patch.object(demo.time, "sleep"), contextlib.redirect_stdout(io.StringIO()):
                    return demo.main()

    def assert_cleanup(self):
        for hand in (self.left, self.right):
            self.assertEqual(hand.method_calls[-3:],
                             [call.stop(), call.disable(), call.disconnect()])

    def test_opposite_waves(self):
        self.assertEqual(self.run_demo(), 0)
        self.left.connect.assert_called_once_with("can0", 1, retry_count=3)
        self.right.connect.assert_called_once_with("can3", 1, retry_count=3)
        self.assertEqual(self.left.set_positions.call_count, 2)
        self.assertEqual(self.right.set_positions.call_count, 2)
        for left_call, right_call in zip(self.left.set_positions.call_args_list,
                                         self.right.set_positions.call_args_list):
            left, right = left_call.args[0], right_call.args[0]
            self.assertEqual(left[:2], [0, 0])
            self.assertEqual(right[:2], [0, 0])
            self.assertTrue(all(0 <= p <= demo.AMPLITUDE for p in left + right))
            self.assertEqual(left[2:], [right[2], right[5], right[4], right[3]])
        self.assert_cleanup()

    def test_right_connection_failure(self):
        self.right.connect.return_value = False
        self.assertEqual(self.run_demo(), 1)
        self.left.enable.assert_not_called()
        self.assert_cleanup()

    def test_cpp_defaults_and_targets(self):
        self.assertEqual(self.run_demo(), 0)
        self.assertEqual((demo.AMPLITUDE, demo.PERIOD, demo.CYCLES, demo.DURATION),
                         (6000, 3.0, 10, 30.0))
        for hand in (self.left, self.right):
            hand.set_speed.assert_called_once_with(20000)
            hand.set_current_limit.assert_called_once_with(400)
        # C++ dry-run reference targets at 1 s (half envelope) and 2 s (full envelope).
        self.assertEqual(self.left.set_positions.call_args_list,
                         [call([0, 0, 2250, 201, 750, 2799]),
                          call([0, 0, 4500, 5598, 1500, 402])])
        self.assertEqual(self.right.set_positions.call_args_list,
                         [call([0, 0, 2250, 2799, 750, 201]),
                          call([0, 0, 4500, 402, 1500, 5598])])

    def test_motion_failure(self):
        self.left.set_positions.return_value = False
        self.assertEqual(self.run_demo(), 1)
        self.assert_cleanup()

    def test_stop_failure_still_disables_both_hands(self):
        self.left.stop.side_effect = RuntimeError("stop failed")
        self.right.stop.return_value = False
        self.assertEqual(self.run_demo(), 1)
        self.assert_cleanup()

    def test_interrupt(self):
        self.left.home.side_effect = KeyboardInterrupt
        self.assertEqual(self.run_demo(), 130)
        self.left.set_positions.assert_not_called()
        self.right.set_positions.assert_not_called()
        self.assert_cleanup()


if __name__ == "__main__":
    unittest.main()
