"""Hardware-free checks against the installed Python package."""
import subprocess
import sys
import unittest

from roboparty_dexhand_sdk import Hand


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.hand = Hand()

    def tearDown(self):
        self.hand.disconnect()

    def test_offline(self):
        self.assertFalse(self.hand.is_connected())
        for name in ("enable", "disable", "stop", "home"):
            self.assertFalse(getattr(self.hand, name)())
        for args in (("", 1), ("can0", 0)):
            self.assertFalse(self.hand.connect(*args))
        self.assertFalse(self.hand.set_positions([0] * 6))
        self.assertFalse(self.hand.set_positions((0,) * 6))
        self.assertFalse(self.hand.set_position(1, 0))
        self.assertFalse(self.hand.set_speed(20000))
        self.assertFalse(self.hand.set_current_limit(400))
        self.assertFalse(self.hand.set_torque_reached_mode(True))
        self.hand.disconnect()
        self.hand.disconnect()

    def test_conversion(self):
        calls = [lambda v: self.hand.connect("can0", v),
                 lambda v: self.hand.set_position(v, 0),
                 lambda v: self.hand.set_position(1, v),
                 self.hand.set_speed, self.hand.set_current_limit,
                 lambda v: self.hand.set_positions([0, 0, v, 0, 0, 0])]
        for call in calls:
            for value in (True, False, 1.0, "1", None):
                with self.subTest(value=value), self.assertRaises(TypeError):
                    call(value)
            for value in (2**31, -(2**31) - 1, 2**100, -(2**100)):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    call(value)
        for value in ([], [0] * 5, [0] * 7):
            with self.assertRaises(ValueError):
                self.hand.set_positions(value)
        for value in ("000000", range(6), None):
            with self.assertRaises(TypeError):
                self.hand.set_positions(value)
        for value in (0, 1, "true", None):
            with self.assertRaises(TypeError):
                self.hand.set_torque_reached_mode(value)
        with self.assertRaises(TypeError):
            self.hand.connect(b"can0", 1)
        self.assertFalse(self.hand.set_positions([10001] * 6))
        self.assertFalse(self.hand.set_speed(-(2**31)))
        self.assertFalse(self.hand.set_speed(2**31 - 1))

    def test_failed_connection_releases_workers(self):
        # Longer than Linux IFNAMSIZ, so this cannot address a real CAN device.
        result = subprocess.run([sys.executable, "-c", """
from roboparty_dexhand_sdk import Hand
hand = Hand()
for _ in range(2):
    assert not hand.connect("sdk_test_interface_name_too_long", 1)
    assert not hand.is_connected()
    assert not hand.get_state().feedback_valid
    hand.disconnect()
"""], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr.count("connect initialization attempt="), 60)
        self.assertEqual(result.stderr.count("connect initialization attempt=30\n"), 2)

    def test_retry_parameter_is_not_public(self):
        with self.assertRaises(TypeError):
            self.hand.connect("", 1, 30)
        with self.assertRaises(TypeError):
            self.hand.connect("", 1, retry_count=30)

    def test_snapshot(self):
        state = self.hand.get_state()
        self.assertFalse(state.feedback_valid)
        for name in ("positions", "velocities", "currents", "enabled", "homed", "torque_reached", "fault_codes"):
            self.assertIsInstance(getattr(state, name), tuple)
            self.assertEqual(len(getattr(state, name)), 6)
            with self.assertRaises(AttributeError):
                setattr(state, name, (1,) * 6)
        with self.assertRaises(AttributeError):
            state.feedback_valid = True
        self.hand.disconnect()
        del self.hand
        self.assertEqual(state.positions, (0,) * 6)
        self.hand = Hand()


if __name__ == "__main__":
    unittest.main()
