#include <dh116s/hand.hpp>
#include <pybind11/pybind11.h>

#include <limits>
#include <memory>

namespace py = pybind11;
namespace {
int integer(py::handle value) {
    if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
        throw py::type_error("expected an integer (bool and float are not accepted)");
    int overflow = 0;
    const long long result = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
    if (PyErr_Occurred()) throw py::error_already_set();
    if (overflow || result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
        throw py::value_error("integer is outside the C++ int representation range");
    return static_cast<int>(result);
}

struct PythonHand {
    std::unique_ptr<dh116s::Hand> hand = std::make_unique<dh116s::Hand>();
    ~PythonHand() {
        // Joining native workers must not prevent other Python threads from progressing.
        if (PyGILState_Check()) {
            py::gil_scoped_release release;
            hand.reset();
        } else {
            hand.reset();
        }
    }
};

template <typename T>
py::tuple snapshot(const std::array<T, 6>& values) {
    py::tuple result(6);
    for (std::size_t i = 0; i < values.size(); ++i) result[i] = py::cast(values[i]);
    return result;
}
}

PYBIND11_MODULE(_core, m) {
    m.doc() = "DH116S native bindings; positions use stroke units, not degrees.";
    py::class_<dh116s::HandState>(m, "HandState")
        .def_property_readonly("positions", [](const dh116s::HandState& s) { return snapshot(s.positions); })
        .def_property_readonly("velocities", [](const dh116s::HandState& s) { return snapshot(s.velocities); })
        .def_property_readonly("currents", [](const dh116s::HandState& s) { return snapshot(s.currents); })
        .def_property_readonly("enabled", [](const dh116s::HandState& s) { return snapshot(s.enabled); })
        .def_property_readonly("homed", [](const dh116s::HandState& s) { return snapshot(s.homed); })
        .def_property_readonly("torque_reached", [](const dh116s::HandState& s) { return snapshot(s.torque_reached); })
        .def_property_readonly("fault_codes", [](const dh116s::HandState& s) { return snapshot(s.fault_codes); })
        .def_readonly("feedback_valid", &dh116s::HandState::feedback_valid);

    auto hand = py::class_<PythonHand>(m, "Hand").def(py::init<>());
    hand.def("connect", [](PythonHand& self, py::object name, py::object node) {
        if (!py::isinstance<py::str>(name)) throw py::type_error("can_name must be str");
        const auto can = name.cast<std::string>();
        const int id = integer(node);
        py::gil_scoped_release release;
        return self.hand->connect(can, id);
    }, py::arg("can_name"), py::arg("node_id"));
    hand.def("set_positions", [](PythonHand& self, py::object values) {
        if (!py::isinstance<py::list>(values) && !py::isinstance<py::tuple>(values))
            throw py::type_error("values must be a list or tuple of six integers");
        const auto seq = values.cast<py::sequence>();
        if (seq.size() != 6) throw py::value_error("values must contain exactly six integers");
        std::array<int, 6> positions{};
        for (std::size_t i = 0; i < positions.size(); ++i) positions[i] = integer(seq[i]);
        py::gil_scoped_release release;
        return self.hand->set_positions(positions);
    }, py::arg("values"));
    hand.def("set_position", [](PythonHand& self, py::object axis, py::object value) {
        const int id = integer(axis), position = integer(value);
        py::gil_scoped_release release;
        return self.hand->set_position(id, position);
    }, py::arg("id"), py::arg("position"));
    hand.def("set_speed", [](PythonHand& self, py::object value) {
        const int speed = integer(value);
        py::gil_scoped_release release;
        return self.hand->set_speed(speed);
    }, py::arg("speed"));
    hand.def("set_current_limit", [](PythonHand& self, py::object value) {
        const int current = integer(value);
        py::gil_scoped_release release;
        return self.hand->set_current_limit(current);
    }, py::arg("current"));
    hand.def("set_torque_reached_mode", [](PythonHand& self, py::object value) {
        if (!PyBool_Check(value.ptr())) throw py::type_error("stop_on_reached must be bool");
        const bool stop = value.cast<bool>();
        py::gil_scoped_release release;
        return self.hand->set_torque_reached_mode(stop);
    }, py::arg("stop_on_reached"));
    hand.def("disconnect", [](PythonHand& s) { s.hand->disconnect(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("is_connected", [](PythonHand& s) { return s.hand->is_connected(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("enable", [](PythonHand& s) { return s.hand->enable(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("disable", [](PythonHand& s) { return s.hand->disable(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("stop", [](PythonHand& s) { return s.hand->stop(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("home", [](PythonHand& s) { return s.hand->home(); }, py::call_guard<py::gil_scoped_release>());
    hand.def("get_state", [](PythonHand& s) { return s.hand->get_state(); }, py::call_guard<py::gil_scoped_release>());
}
