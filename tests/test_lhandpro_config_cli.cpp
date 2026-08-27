// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_driver.hpp"
#include "drivers/lhandpro/lhandpro_feedback_period.hpp"
#include "fakes/fake_canfd_transport.hpp"
#include "fakes/fake_lhandpro_sdk.hpp"
#include "test_support.hpp"
#include "tools/lhandpro_config_cli.hpp"

#include <linux/can.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace roboparty::dexhand::detail;

namespace roboparty::dexhand::detail {

int finish_lhandpro_config_cli(
    int result, const std::string& primary_diagnostic, std::ostream& error,
    const std::function<void()>& cleanup);

}  // namespace roboparty::dexhand::detail

namespace {

struct DriverRecord {
  int factory_calls{0};
  FakeLHandProSdk* sdk{nullptr};
  FakeCanFdTransport* transport{nullptr};
  std::vector<std::string> sdk_calls;
  int destroy_calls{0};
  int transport_close_calls_at_destroy{0};
  int transport_clear_calls_at_destroy{0};
  bool transport_open_at_destroy{false};
  bool monitor_started_at_destroy{false};
};

struct Invocation {
  int code{-1};
  std::string output;
  std::string error;
};

using Configure = std::function<void(FakeLHandProSdk&, FakeCanFdTransport&)>;

ConfigDriverFactory recording_factory(DriverRecord& record,
                                      Configure configure = {}) {
  return [&record, configure = std::move(configure)](
             const std::string& interface, int node_id) {
    ++record.factory_calls;
    auto sdk = std::make_unique<FakeLHandProSdk>();
    auto transport = std::make_unique<FakeCanFdTransport>();
    record.sdk = sdk.get();
    record.transport = transport.get();
    if (configure) configure(*sdk, *transport);
    auto configured_before_call = std::move(sdk->before_call);
    sdk->before_call = [&record,
                        configured_before_call =
                            std::move(configured_before_call)](
                           const std::string& operation) {
      if (configured_before_call) configured_before_call(operation);
      record.sdk_calls.push_back(operation);
      if (operation != "destroy") return;
      ++record.destroy_calls;
      record.transport_close_calls_at_destroy =
          record.transport->close_calls.load();
      record.transport_clear_calls_at_destroy =
          record.transport->clear_calls.load();
      record.transport_open_at_destroy = record.transport->is_open();
      record.monitor_started_at_destroy = record.sdk->monitor_started;
    };
    return std::make_unique<LHandProDriver>(
        interface, LHandProModel::Dof6S, node_id, std::move(sdk),
        std::move(transport));
  };
}

Invocation invoke(const std::vector<std::string>& arguments,
                  const ConfigDriverFactory& factory) {
  std::vector<const char*> argv;
  argv.reserve(arguments.size());
  for (const auto& argument : arguments) argv.push_back(argument.c_str());
  std::ostringstream output;
  std::ostringstream error;
  const int code = run_lhandpro_config_cli(
      static_cast<int>(argv.size()), argv.data(), output, error, factory);
  return {code, output.str(), error.str()};
}

Invocation invoke(const std::vector<std::string>& arguments,
                  DriverRecord& record, Configure configure = {}) {
  return invoke(arguments, recording_factory(record, std::move(configure)));
}

void check_contains(const std::string& text, const std::string& expected) {
  CHECK(text.find(expected) != std::string::npos);
}

void check_not_contains(const std::string& text,
                        const std::string& unexpected) {
  CHECK(text.find(unexpected) == std::string::npos);
}

int count_substring(const std::string& text, const std::string& needle) {
  int count = 0;
  std::size_t position = 0;
  while ((position = text.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

int count_call(const DriverRecord& record, const std::string& operation) {
  int count = 0;
  for (const auto& call : record.sdk_calls) {
    if (call == operation) ++count;
  }
  return count;
}

void check_no_motion_calls(const DriverRecord& record) {
  CHECK_EQ(count_call(record, "set_enable"), 0);
  CHECK_EQ(count_call(record, "home_motors"), 0);
  CHECK_EQ(count_call(record, "move_motors"), 0);
  CHECK_EQ(count_call(record, "stop_motors"), 0);
  CHECK_EQ(count_call(record, "set_move_no_home"), 0);
}

void check_cleanup(const DriverRecord& record) {
  CHECK_EQ(record.destroy_calls, 1);
  CHECK_EQ(record.transport_close_calls_at_destroy, 1);
  CHECK_EQ(record.transport_clear_calls_at_destroy, 1);
  CHECK(!record.transport_open_at_destroy);
  CHECK(!record.monitor_started_at_destroy);
  CHECK_EQ(count_call(record, "stop_monitor"), 1);
  CHECK_EQ(count_call(record, "close"), 1);
  CHECK_EQ(count_call(record, "clear_tx"), 1);
  check_no_motion_calls(record);
}

void check_help_and_usage_errors_do_not_construct_driver() {
  for (const auto& arguments :
       std::vector<std::vector<std::string>>{
           {"roboparty-dexhand-config", "--help"},
           {"roboparty-dexhand-config", "feedback-period", "show", "bogus",
            "--help"},
           {"roboparty-dexhand-config", "feedback-period", "apply",
            "--node-id", "bad", "--help"}}) {
    DriverRecord record;
    const auto result = invoke(arguments, record);
    CHECK_EQ(result.code, 0);
    CHECK_EQ(record.factory_calls, 0);
    CHECK(result.error.empty());
    check_contains(result.output, "feedback-period show");
    check_contains(result.output, "feedback-period apply");
  }

  const std::vector<std::vector<std::string>> invalid{
      {"roboparty-dexhand-config"},
      {"roboparty-dexhand-config", "feedback-period"},
      {"roboparty-dexhand-config", "wrong", "show"},
      {"roboparty-dexhand-config", "feedback-period", "wrong"},
      {"roboparty-dexhand-config", "feedback-period", "show"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--node-id",
       "1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--node-id"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--unknown",
       "x", "--interface", "can0", "--node-id", "1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "extra",
       "--interface", "can0", "--node-id", "1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--interface", "can1", "--node-id", "1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1", "--node-id", "2"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "", "--node-id", "1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "0"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "128"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "+1"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1x"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "999999999999999999999999999"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1", "--save"},
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--save"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "19", "--save"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20x", "--save"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20",
       "--milliseconds", "20", "--save"},
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20", "--save",
       "--save"},
  };
  for (const auto& arguments : invalid) {
    DriverRecord record;
    const auto result = invoke(arguments, record);
    CHECK_EQ(result.code, 2);
    CHECK_EQ(record.factory_calls, 0);
    CHECK(result.output.empty());
    CHECK(!result.error.empty());
  }
}

void check_show_success_with_reordered_options() {
  DriverRecord record;
  const auto result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--node-id",
       "7", "--interface", "can-test"},
      record, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size();
             ++axis) {
          sdk.set_sdo_value(kFeedbackPeriodIndexes[axis],
                            static_cast<unsigned int>(101 + axis));
        }
      });
  CHECK_EQ(result.code, 0);
  CHECK_EQ(record.factory_calls, 1);
  CHECK(result.error.empty());
  check_contains(result.output, "interface=can-test node-id=7\n");
  CHECK_EQ(count_substring(result.output, "result="), 1);
  check_contains(result.output, "result=shown\n");
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    std::ostringstream expected;
    expected << "axis=" << (axis + 1) << " index=0x" << std::uppercase
             << std::hex << kFeedbackPeriodIndexes[axis] << std::dec
             << " before=" << (101 + axis) << " after=" << (101 + axis)
             << '\n';
    check_contains(result.output, expected.str());
  }
  CHECK_EQ(count_call(record, "get_sdo_drive_param"), 6);
  CHECK_EQ(count_call(record, "set_sdo_drive_param"), 0);
  CHECK_EQ(count_call(record, "save_sdo_drive_param"), 0);
  check_cleanup(record);
}

void check_apply_success_outcomes() {
  DriverRecord compliant;
  const auto already = invoke(
      {"roboparty-dexhand-config", "feedback-period", "apply", "--save",
       "--milliseconds", "20", "--node-id", "3", "--interface", "canA"},
      compliant);
  CHECK_EQ(already.code, 0);
  CHECK_EQ(count_substring(already.output, "result="), 1);
  check_contains(already.output, "result=already-compliant\n");
  check_contains(already.output, "save-attempted=0");
  check_not_contains(already.output, "power-cycle");
  CHECK_EQ(count_call(compliant, "set_sdo_drive_param"), 0);
  CHECK_EQ(count_call(compliant, "save_sdo_drive_param"), 0);
  check_cleanup(compliant);

  DriverRecord changed;
  const auto saved = invoke(
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "canB", "--save", "--node-id", "4", "--milliseconds", "20"},
      changed, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 100U);
      });
  CHECK_EQ(saved.code, 0);
  CHECK_EQ(count_substring(saved.output, "result="), 1);
  check_contains(saved.output, "result=saved\n");
  check_contains(saved.output, "save-attempted=1");
  check_contains(saved.output, "power-cycle");
  check_contains(saved.output, "show");
  CHECK_EQ(count_call(changed, "set_sdo_drive_param"), 6);
  CHECK_EQ(count_call(changed, "save_sdo_drive_param"), 1);
  check_cleanup(changed);
}

void check_partial_show_failure_output() {
  DriverRecord record;
  const auto result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1"},
      record, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 123U);
        sdk.script_result("get_sdo_drive_param", 0);
        sdk.script_result("get_sdo_drive_param", 41);
      });
  CHECK_EQ(result.code, 1);
  check_contains(result.output, "result=read-failed\n");
  check_contains(result.output,
                 "axis=1 index=0x201D before=123\n");
  check_not_contains(result.output, "axis=2 index=");
  check_not_contains(result.output, " after=");
  check_contains(result.output,
                 "failure-operation=get_sdo_drive_param failure-axis=2 "
                 "failure-code=41\n");
  check_contains(result.output, "rollback-attempted=0\n");
  check_contains(result.output, "rollback-verified=0\n");
  check_contains(result.output, "save-attempted=0\n");
  check_cleanup(record);
}

void check_rollback_failure_outputs() {
  DriverRecord restored;
  const auto restored_result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20", "--save"},
      restored, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 100U);
        sdk.script_result("set_sdo_drive_param", 0);
        sdk.script_result("set_sdo_drive_param", 52);
      });
  CHECK_EQ(restored_result.code, 1);
  check_contains(restored_result.output, "result=failed-restored\n");
  check_contains(restored_result.output,
                 "failure-operation=set_sdo_drive_param failure-axis=2 "
                 "failure-code=52\n");
  check_contains(restored_result.output, "rollback-attempted=1\n");
  check_contains(restored_result.output, "rollback-verified=1\n");
  check_contains(restored_result.output, "save-attempted=0\n");
  CHECK_EQ(count_substring(restored_result.output, " after="), 6);
  check_cleanup(restored);

  DriverRecord uncertain;
  const auto uncertain_result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20", "--save"},
      uncertain, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 100U);
        sdk.script_result("set_sdo_drive_param", 0);
        sdk.script_result("set_sdo_drive_param", 53);
        for (int read = 0; read < 6; ++read) {
          sdk.script_result("get_sdo_drive_param", 0);
        }
        sdk.script_result("get_sdo_drive_param", 54);
      });
  CHECK_EQ(uncertain_result.code, 1);
  check_contains(uncertain_result.output, "result=failed-uncertain\n");
  check_contains(uncertain_result.output, "rollback-attempted=1\n");
  check_contains(uncertain_result.output, "rollback-verified=0\n");
  CHECK_EQ(count_substring(uncertain_result.output, " after="), 0);
  check_cleanup(uncertain);
}

void check_save_failure_has_no_retry() {
  DriverRecord record;
  const auto result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "apply", "--interface",
       "can0", "--node-id", "1", "--milliseconds", "20", "--save"},
      record, [](FakeLHandProSdk& sdk, FakeCanFdTransport&) {
        sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 100U);
        sdk.script_result("save_sdo_drive_param", 61);
      });
  CHECK_EQ(result.code, 1);
  check_contains(result.output, "result=save-failed\n");
  check_contains(result.output,
                 "failure-operation=save_sdo_drive_param failure-axis=0 "
                 "failure-code=61\n");
  check_contains(result.output, "save-attempted=1\n");
  CHECK_EQ(count_call(record, "save_sdo_drive_param"), 1);
  check_cleanup(record);
}

void check_init_and_operation_failures_cleanup() {
  DriverRecord initialization;
  const auto init_failed = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "bad0", "--node-id", "1"},
      initialization, [](FakeLHandProSdk&, FakeCanFdTransport& transport) {
        transport.open_result = false;
      });
  CHECK_EQ(init_failed.code, 1);
  check_contains(init_failed.output, "interface=bad0 node-id=1\n");
  check_contains(init_failed.error, "initialization failed");
  CHECK_EQ(initialization.factory_calls, 1);
  CHECK_EQ(count_call(initialization, "destroy"), 1);
  check_no_motion_calls(initialization);

  DriverRecord asynchronous;
  const auto operation_failed = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "2"},
      asynchronous, [](FakeLHandProSdk& sdk, FakeCanFdTransport& transport) {
        sdk.fail_operation = "decode_canfd";
        auto* transport_ptr = &transport;
        sdk.before_call = [transport_ptr](const std::string& operation) {
          if (operation != "get_sdo_drive_param") return;
          CanFdFrame frame;
          frame.id = 0x582;
          frame.len = 8;
          transport_ptr->deliver(frame);
        };
      });
  CHECK_EQ(operation_failed.code, 1);
  check_contains(operation_failed.error, "decode_canfd");
  CHECK_EQ(count_substring(operation_failed.output, "result="), 0);
  check_cleanup(asynchronous);
}

void check_factory_runtime_failures() {
  int throw_calls = 0;
  const auto throws = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1"},
      [&throw_calls](const std::string&, int) -> std::unique_ptr<LHandProDriver> {
        ++throw_calls;
        throw std::runtime_error("factory exploded");
      });
  CHECK_EQ(throws.code, 1);
  CHECK_EQ(throw_calls, 1);
  check_contains(throws.error, "factory exploded");

  int null_calls = 0;
  const auto null_result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1"},
      [&null_calls](const std::string&, int) {
        ++null_calls;
        return std::unique_ptr<LHandProDriver>{};
      });
  CHECK_EQ(null_result.code, 1);
  CHECK_EQ(null_calls, 1);
  check_contains(null_result.error, "factory returned null");

  const ConfigDriverFactory absent;
  const auto absent_result = invoke(
      {"roboparty-dexhand-config", "feedback-period", "show", "--interface",
       "can0", "--node-id", "1"},
      absent);
  CHECK_EQ(absent_result.code, 1);
  check_contains(absent_result.error, "factory unavailable");
}

void check_cleanup_exception_diagnostics() {
  bool cleanup_called = false;
  std::ostringstream success_error;
  const int success_cleanup_result = finish_lhandpro_config_cli(
      0, "", success_error, [&] {
        cleanup_called = true;
        throw std::runtime_error("cleanup exploded after success");
      });
  CHECK(cleanup_called);
  CHECK_EQ(success_cleanup_result, 1);
  CHECK_EQ(success_error.str(),
           std::string("cleanup failed: cleanup exploded after success\n"));

  cleanup_called = false;
  std::ostringstream primary_error;
  const int primary_cleanup_result = finish_lhandpro_config_cli(
      1, "operation failed: primary exploded", primary_error, [&] {
        cleanup_called = true;
        throw std::runtime_error("cleanup exploded after primary");
      });
  CHECK(cleanup_called);
  CHECK_EQ(primary_cleanup_result, 1);
  CHECK_EQ(primary_error.str(),
           std::string("operation failed: primary exploded\n"
                       "cleanup failed: cleanup exploded after primary\n"));
}

}  // namespace

int main() {
  check_help_and_usage_errors_do_not_construct_driver();
  check_show_success_with_reordered_options();
  check_apply_success_outcomes();
  check_partial_show_failure_output();
  check_rollback_failure_outputs();
  check_save_failure_has_no_retry();
  check_init_and_operation_failures_cleanup();
  check_factory_runtime_failures();
  check_cleanup_exception_diagnostics();
  return 0;
}
