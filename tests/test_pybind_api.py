# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

import unittest

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


if __name__ == "__main__":
    unittest.main()
