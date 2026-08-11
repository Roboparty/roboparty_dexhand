// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include <pybind11/pybind11.h>

#include "hand_driver.hpp"

namespace py = pybind11;

PYBIND11_MODULE(dexhand_py, module) {
  module.doc() = "Dexterous Hand Driver Python SDK (roboparty_dexhand)";

  py::enum_<HandCommType>(module, "HandCommType")
      .value("CANFD", HandCommType::CANFD)
      .export_values();
  py::enum_<HandModel>(module, "HandModel")
      .value("LHANDPRO_6DOF", HAND_LHANDPRO_6DOF)
      .value("LHANDPRO_16DOF", HAND_LHANDPRO_16DOF)
      .export_values();

  py::class_<HandDriver, std::shared_ptr<HandDriver>>(module, "HandDriver")
      .def_static("create_hand", &HandDriver::create_hand,
                  py::arg("hand_type"), py::arg("interface_type"),
                  py::arg("interface"),
                  py::arg("hand_model") = HAND_LHANDPRO_6DOF,
                  py::arg("canfd_node_id") = 1)
      .def("init_hand", &HandDriver::init_hand,
           py::arg("enable_motors") = true,
           py::arg("home_motors") = true,
           py::arg("home_wait_time") = 5.0F,
           py::call_guard<py::gil_scoped_release>())
      .def("deinit_hand", &HandDriver::deinit_hand,
           py::call_guard<py::gil_scoped_release>())
      .def("move_motors", &HandDriver::move_motors,
           py::arg("finger_id") = 0)
      .def("stop_motors", &HandDriver::stop_motors,
           py::arg("finger_id") = 0)
      .def("set_target_position", &HandDriver::set_target_position,
           py::arg("finger_id"), py::arg("position"))
      .def("set_target_angle", &HandDriver::set_target_angle,
           py::arg("finger_id"), py::arg("angle"))
      .def("set_position_velocity", &HandDriver::set_position_velocity,
           py::arg("finger_id"), py::arg("velocity"))
      .def("set_max_current", &HandDriver::set_max_current,
           py::arg("finger_id"), py::arg("current"))
      .def("set_enable", &HandDriver::set_enable,
           py::arg("finger_id"), py::arg("enable"))
      .def("home_motors", &HandDriver::home_motors,
           py::arg("finger_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("set_move_no_home", &HandDriver::set_move_no_home,
           py::arg("enable"))
      .def("get_now_position", &HandDriver::get_now_position,
           py::arg("finger_id"))
      .def("get_now_angle", &HandDriver::get_now_angle,
           py::arg("finger_id"))
      .def("get_now_status", &HandDriver::get_now_status,
           py::arg("finger_id"))
      .def("get_now_current", &HandDriver::get_now_current,
           py::arg("finger_id"))
      .def("get_now_alarm", &HandDriver::get_now_alarm,
           py::arg("finger_id"))
      .def("clear_alarm", &HandDriver::clear_alarm,
           py::arg("finger_id") = 0)
      .def("get_dof", [](HandDriver& self) {
        int total = 0;
        int active = 0;
        self.get_dof(total, active);
        return py::make_tuple(total, active);
      })
      .def("get_can_name", &HandDriver::get_can_name);
}
