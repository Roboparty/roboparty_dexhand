# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

import builtins
import ctypes
import importlib.util
from pathlib import Path
import unittest
from unittest import mock

import dexhand_py


class DexhandApiTest(unittest.TestCase):
    def test_enums_are_exact(self):
        self.assertEqual(int(dexhand_py.HandCommType.CANFD.value), 0)
        self.assertFalse(hasattr(dexhand_py.HandCommType, "ETHERCAT"))
        self.assertFalse(hasattr(dexhand_py.HandCommType, "RS485"))
        self.assertEqual(int(dexhand_py.HandModel.LHANDPRO_6DOF.value), 0)
        self.assertEqual(int(dexhand_py.HandModel.LHANDPRO_16DOF.value), 1)

    def test_factory_defaults_and_removed_keywords(self):
        hand = dexhand_py.HandDriver.create_hand("LHandPro", "canfd", "can0")
        self.assertEqual(hand.get_can_name(), "can0")
        with self.assertRaises(TypeError):
            dexhand_py.HandDriver.create_hand(
                "LHandPro", "canfd", "can0", canfd_nom_baudrate=1000000)
        with self.assertRaises(TypeError):
            dexhand_py.HandDriver.create_hand(
                "LHandPro", "canfd", "can0", canfd_dat_baudrate=5000000)

    def test_factory_accepts_explicit_model_and_node_keywords(self):
        hand = dexhand_py.HandDriver.create_hand(
            "LHandPro",
            "canfd",
            "can0",
            hand_model=dexhand_py.HandModel.LHANDPRO_16DOF,
            canfd_node_id=2,
        )
        self.assertEqual(hand.get_can_name(), "can0")

    def test_complete_base_api(self):
        expected = {
            "create_hand", "init_hand", "deinit_hand", "move_motors",
            "stop_motors", "set_target_position", "set_target_angle",
            "set_position_velocity", "set_max_current", "set_enable",
            "home_motors", "set_move_no_home", "get_now_position",
            "get_now_angle", "get_now_status", "get_now_current",
            "get_now_alarm", "clear_alarm", "get_dof", "get_can_name",
        }
        public_names = {
            name for name in dir(dexhand_py.HandDriver)
            if not name.startswith("_")
        }
        self.assertEqual(public_names, expected)

    def test_vendor_subclass_is_not_exported(self):
        self.assertFalse(hasattr(dexhand_py, "LHandProDriver"))


class ManualHardwareHelperTest(unittest.TestCase):
    def load_manual_module(self):
        project_root = Path(__file__).resolve().parent.parent
        script = project_root / "scripts" / "test_dexhand.py"
        imported_modules = []
        real_import = builtins.__import__

        def tracking_import(name, *args, **kwargs):
            imported_modules.append(name)
            return real_import(name, *args, **kwargs)

        spec = importlib.util.spec_from_file_location(
            "dexhand_manual_test_contract", script)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        with mock.patch("builtins.__import__", side_effect=tracking_import):
            with mock.patch.object(ctypes, "CDLL") as cdll:
                spec.loader.exec_module(module)
        cdll.assert_not_called()
        self.assertNotIn("dexhand_py", imported_modules)
        return module

    def test_import_has_no_cdll_or_dexhand_import_side_effect(self):
        module = self.load_manual_module()
        self.assertEqual(module.main.__defaults__, (None,))

    def test_sequence_deinitializes_once_on_every_exit(self):
        module = self.load_manual_module()

        class FakeHand:
            def __init__(self, init_result=True, failure=None):
                self.init_result = init_result
                self.failure = failure
                self.deinit_calls = 0

            def maybe_fail(self, stage):
                if self.failure is None or self.failure[0] != stage:
                    return
                raise self.failure[1](f"fake {stage} failure")

            def init_hand(self, **kwargs):
                self.maybe_fail("init")
                return self.init_result

            def get_dof(self):
                return (6, 1)

            def set_target_position(self, joint, position):
                pass

            def set_position_velocity(self, joint, velocity):
                pass

            def move_motors(self, joint):
                self.maybe_fail("motion")

            def get_now_position(self, joint):
                self.maybe_fail("feedback")
                return 0

            def deinit_hand(self):
                self.deinit_calls += 1

        cases = (
            ("init false", False, None, None, 1),
            (
                "init exception", True, ("init", RuntimeError),
                RuntimeError, None,
            ),
            (
                "motion exception", True, ("motion", RuntimeError),
                RuntimeError, None,
            ),
            (
                "feedback exception", True, ("feedback", RuntimeError),
                RuntimeError, None,
            ),
            (
                "motion interrupt", True, ("motion", KeyboardInterrupt),
                KeyboardInterrupt, None,
            ),
            (
                "feedback interrupt", True, ("feedback", KeyboardInterrupt),
                KeyboardInterrupt, None,
            ),
            ("success", True, None, None, 0),
        )
        for case in cases:
            (
                name, init_result, failure,
                expected_exception, expected_result,
            ) = case
            with self.subTest(name=name):
                hand = FakeHand(init_result=init_result, failure=failure)
                model = object()
                factory_calls = []

                def create_hand(**kwargs):
                    factory_calls.append(kwargs)
                    return hand

                if expected_exception is None:
                    result = module.run_hand_sequence(
                        create_hand,
                        model,
                        sleep_fn=lambda _: None,
                        output=lambda *_: None,
                    )
                    self.assertEqual(result, expected_result)
                else:
                    with self.assertRaises(expected_exception):
                        module.run_hand_sequence(
                            create_hand,
                            model,
                            sleep_fn=lambda _: None,
                            output=lambda *_: None,
                        )
                self.assertEqual(hand.deinit_calls, 1)
                self.assertEqual(len(factory_calls), 1)
                self.assertIs(factory_calls[0]["hand_model"], model)
                self.assertEqual(factory_calls[0]["canfd_node_id"], 1)


if __name__ == "__main__":
    unittest.main()
