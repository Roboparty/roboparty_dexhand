// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_driver.hpp"
#include "drivers/lhandpro/lhandpro_feedback_period.hpp"
#include "fakes/fake_canfd_transport.hpp"
#include "fakes/fake_lhandpro_sdk.hpp"
#include "test_support.hpp"

#include <linux/can.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace roboparty::dexhand::detail;

namespace {

using namespace std::chrono_literals;

struct Fixture {
  FakeLHandProSdk* sdk{nullptr};
  FakeCanFdTransport* transport{nullptr};
  std::unique_ptr<LHandProDriver> driver;

  explicit Fixture(LHandProModel model = LHandProModel::Dof16, int node = 1) {
    auto sdk_owner = std::make_unique<FakeLHandProSdk>();
    auto transport_owner = std::make_unique<FakeCanFdTransport>();
    sdk = sdk_owner.get();
    transport = transport_owner.get();
    if (model == LHandProModel::Dof16) {
      sdk->total_dof = 21;
      sdk->active_dof = 16;
    }
    driver = std::make_unique<LHandProDriver>(
        "can-test", model, node, std::move(sdk_owner),
        std::move(transport_owner));
  }

  void fail(const std::string& operation) {
    if (operation == "transport.open") {
      transport->open_result = false;
    } else {
      sdk->fail_operation = operation;
    }
  }

  void clear_failure() {
    transport->open_result = true;
    sdk->fail_operation.clear();
  }

  bool released_once() const {
    return driver->state_for_test() == DriverState::Created && !sdk->created &&
           !transport->is_open() && sdk->tx_callback == nullptr &&
           sdk->count("destroy") <= 1 &&
           transport->close_calls.load() <= 1;
  }
};

class DofSnapshotProbe final : public HandDriver {
 public:
  void set_dof(int total, int active) {
    std::lock_guard<std::mutex> lock(dof_mutex_);
    dof_total_ = total;
    dof_active_ = active;
  }

  void set_dof_with_barrier(int total, int active,
                            std::promise<void>& first_value_written,
                            const std::shared_future<void>& release) {
    std::lock_guard<std::mutex> lock(dof_mutex_);
    dof_total_ = total;
    first_value_written.set_value();
    release.wait();
    dof_active_ = active;
  }

  bool init_hand(bool, bool, float) override { return false; }
  void deinit_hand() override {}
  void move_motors(int) override {}
  void stop_motors(int) override {}
  void set_target_position(int, int) override {}
  void set_target_angle(int, float) override {}
  void set_position_velocity(int, int) override {}
  void set_max_current(int, int) override {}
  void set_enable(int, bool) override {}
  void home_motors(int) override {}
  void set_move_no_home(int) override {}
  int get_now_position(int) override { return 0; }
  float get_now_angle(int) override { return 0.0F; }
  int get_now_status(int) override { return 0; }
  int get_now_current(int) override { return 0; }
  int get_now_alarm(int) override { return 0; }
  void clear_alarm(int) override {}
};

template <typename Predicate>
bool wait_until(Predicate predicate,
                std::chrono::milliseconds timeout = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::yield();
  }
  return predicate();
}

template <typename Callable>
bool throws_invalid_argument(Callable&& callable) {
  try {
    std::forward<Callable>(callable)();
  } catch (const std::invalid_argument&) {
    return true;
  } catch (...) {
  }
  return false;
}

class PromiseReleaseGuard final {
 public:
  explicit PromiseReleaseGuard(std::promise<void>& promise) noexcept
      : promise_(promise) {}

  ~PromiseReleaseGuard() noexcept { release(); }

  void release() noexcept {
    if (released_) return;
    released_ = true;
    try {
      promise_.set_value();
    } catch (...) {
    }
  }

 private:
  std::promise<void>& promise_;
  bool released_{false};
};

std::unique_ptr<LHandProDriver> make_driver(
    std::string interface, LHandProModel model, int node,
    std::unique_ptr<LHandProSdk> sdk = std::make_unique<FakeLHandProSdk>(),
    std::unique_ptr<CanFdTransport> transport =
        std::make_unique<FakeCanFdTransport>()) {
  return std::make_unique<LHandProDriver>(
      std::move(interface), model, node, std::move(sdk),
      std::move(transport));
}

template <typename Exception, typename Callable>
std::string expect_exception(Callable&& callable) {
  try {
    std::forward<Callable>(callable)();
  } catch (const Exception& error) {
    return error.what();
  } catch (...) {
    CHECK(false);
  }
  CHECK(false);
  return {};
}

std::string fault_message(const std::string& operation, int code,
                          const std::string& source) {
  return "LHandPro " + operation + " failed: code=" + std::to_string(code) +
         ", source=" + source;
}

void check_fault_message(const std::string& message,
                         const std::string& operation, int code,
                         const std::string& source) {
  CHECK_EQ(message, fault_message(operation, code, source));
}

void check_fault_report_entry(const std::string& report,
                              const std::string& operation, int code,
                              const std::string& source,
                              std::size_t expected_count = 1) {
  const auto entry = fault_message(operation, code, source);
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = report.find(entry, position)) != std::string::npos) {
    ++count;
    position += entry.size();
  }
  CHECK_EQ(count, expected_count);
}

void check_safety_trio_once(const Fixture& fixture) {
  CHECK_EQ(fixture.sdk->count_stop_motors(0), 1);
  CHECK_EQ(fixture.sdk->count_set_enable(0, false), 1);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(0), 1);
}

void check_no_motion_state_calls(const Fixture& fixture) {
  CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
  CHECK_EQ(fixture.sdk->count_set_enable(false), 0);
  CHECK_EQ(fixture.sdk->count("home_motors"), 0);
  CHECK_EQ(fixture.sdk->count("move_motors"), 0);
  CHECK_EQ(fixture.sdk->count("stop_motors"), 0);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(0), 0);
}

bool is_runtime_feedback_every_base_period_frame(const CanFdFrame& frame,
                                                 std::uint32_t node_id) {
  constexpr std::array<std::uint8_t, 6> payload{
      0x00U, 0x04U, 0x50U, 0x01U, 0x5AU, 0x01U};
  return frame.id == 0x500U + node_id && !frame.extended && !frame.brs &&
         frame.len == payload.size() &&
         std::equal(payload.begin(), payload.end(), frame.data.begin()) &&
         std::all_of(std::next(frame.data.begin(), payload.size()),
                     frame.data.end(),
                     [](std::uint8_t value) { return value == 0U; });
}

std::size_t count_runtime_feedback_every_base_period_frames(
    const FakeCanFdTransport& transport, std::uint32_t node_id) {
  const auto frames = transport.sent_snapshot();
  return static_cast<std::size_t>(std::count_if(
      frames.begin(), frames.end(), [node_id](const CanFdFrame& frame) {
        return is_runtime_feedback_every_base_period_frame(frame, node_id);
      }));
}

CanFdFrame sdo_response(std::uint8_t command, unsigned int index,
                        unsigned char subindex, int node_id = 1) {
  CanFdFrame frame;
  frame.id = static_cast<std::uint32_t>(0x580 + node_id);
  frame.len = 8;
  frame.data[0] = command;
  frame.data[1] = static_cast<std::uint8_t>(index & 0xFFU);
  frame.data[2] = static_cast<std::uint8_t>((index >> 8U) & 0xFFU);
  frame.data[3] = subindex;
  return frame;
}

CanFdFrame realtime_feedback(std::uint8_t type, int node_id = 1) {
  CanFdFrame frame;
  frame.id = static_cast<std::uint32_t>(0x480 + node_id);
  frame.len = 2;
  frame.data[0] = type;
  return frame;
}

CanFdFrame save_compatibility_probe_response(int node_id = 1) {
  auto frame = sdo_response(0x00U, 0x1010U, 0x00U, node_id);
  frame.data[4] = 0x20U;
  return frame;
}

void deliver_latest_write_response(Fixture& fixture, std::uint8_t command) {
  const auto attempts = fixture.sdk->sdo_write_attempt_snapshot();
  CHECK(!attempts.empty());
  const auto& request = attempts.back();
  fixture.transport->deliver(
      sdo_response(command, request.index, request.subindex));
}

void install_early_sdo_acknowledgements(Fixture& fixture) {
  fixture.sdk->before_call = [&fixture](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      deliver_latest_write_response(fixture, 0x60U);
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));
    }
  };
}

void check_safety_trio_order(const Fixture& fixture) {
  const auto events = fixture.sdk->event_snapshot();
  const auto stop_reverse =
      std::find(events.rbegin(), events.rend(), "stop_motors");
  CHECK(stop_reverse != events.rend());
  const auto stop = std::prev(stop_reverse.base());
  const auto disable = std::find(std::next(stop), events.end(), "set_enable");
  CHECK(disable != events.end());
  const auto no_home =
      std::find(std::next(disable), events.end(), "set_move_no_home");
  CHECK(no_home != events.end());
  const auto stop_monitor =
      std::find(std::next(no_home), events.end(), "stop_monitor");
  const auto close = std::find(std::next(no_home), events.end(), "close");
  CHECK(close != events.end());
  if (fixture.sdk->count("start_monitor") != 0) {
    CHECK(stop_monitor != events.end());
    CHECK(stop_monitor < close);
  }
}

void check_fully_released(const Fixture& fixture) {
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
  CHECK(!fixture.sdk->created);
  CHECK(!fixture.sdk->monitor_started);
  CHECK(fixture.sdk->tx_callback == nullptr);
  CHECK(!fixture.transport->is_open());
  CHECK(!fixture.transport->callback_active());
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  CHECK_EQ(fixture.sdk->count("close"), 1);
  CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
  CHECK_EQ(fixture.transport->clear_calls.load(), 1);
  CHECK_EQ(fixture.transport->close_calls.load(), 1);
}

void check_cleanup_resources_are_live(Fixture& fixture,
                                      int& safety_observations,
                                      const std::string& operation,
                                      LHandProDriver* driver = nullptr) {
  const bool is_stop =
      operation == "stop_motors" && fixture.sdk->last_stop_id == 0;
  const bool is_disable = operation == "set_enable" &&
                          fixture.sdk->last_enable_id == 0 &&
                          !fixture.sdk->last_enable;
  const bool is_no_home = operation == "set_move_no_home" &&
                          fixture.sdk->last_move_no_home == 0;
  if (!is_stop && !is_disable && !is_no_home) return;

  if (driver == nullptr) driver = fixture.driver.get();
  CHECK(driver != nullptr);
  CHECK_EQ(driver->state_for_test(), DriverState::Stopping);
  CHECK(fixture.sdk->created);
  CHECK(fixture.transport->is_open());
  CHECK(fixture.transport->callback_active());
  CHECK(fixture.sdk->tx_callback != nullptr);
  if (fixture.sdk->count("start_monitor") != 0) {
    CHECK(fixture.sdk->monitor_started);
  }
  ++safety_observations;
}

struct DriverCall {
  std::string operation;
  std::function<void(Fixture&)> invoke;
};

std::vector<DriverCall> all_public_sdk_calls() {
  return {
      {"move_motors", [](Fixture& fixture) {
         fixture.driver->move_motors(2);
       }},
      {"stop_motors", [](Fixture& fixture) {
         fixture.driver->stop_motors(2);
       }},
      {"set_target_position", [](Fixture& fixture) {
         fixture.driver->set_target_position(2, 100);
       }},
      {"set_target_angle", [](Fixture& fixture) {
         fixture.driver->set_target_angle(2, 10.0F);
       }},
      {"set_position_velocity", [](Fixture& fixture) {
         fixture.driver->set_position_velocity(2, 20);
       }},
      {"set_max_current", [](Fixture& fixture) {
         fixture.driver->set_max_current(2, 30);
       }},
      {"set_enable", [](Fixture& fixture) {
         fixture.driver->set_enable(2, true);
       }},
      {"home_motors", [](Fixture& fixture) {
         fixture.driver->home_motors(2);
       }},
      {"set_move_no_home", [](Fixture& fixture) {
         fixture.driver->set_move_no_home(1);
       }},
      {"clear_alarm", [](Fixture& fixture) {
         fixture.driver->clear_alarm(2);
       }},
      {"get_now_position", [](Fixture& fixture) {
         (void)fixture.driver->get_now_position(2);
       }},
      {"get_now_angle", [](Fixture& fixture) {
         (void)fixture.driver->get_now_angle(2);
       }},
      {"get_now_status", [](Fixture& fixture) {
         (void)fixture.driver->get_now_status(2);
       }},
      {"get_now_current", [](Fixture& fixture) {
         (void)fixture.driver->get_now_current(2);
       }},
      {"get_now_alarm", [](Fixture& fixture) {
         (void)fixture.driver->get_now_alarm(2);
       }},
  };
}

void check_not_ready_calls_throw_without_sdk(Fixture& fixture) {
  for (const auto& call : all_public_sdk_calls()) {
    const int calls_before = fixture.sdk->count(call.operation);
    (void)expect_exception<std::logic_error>([&] { call.invoke(fixture); });
    CHECK_EQ(fixture.sdk->count(call.operation), calls_before);
  }

  const int enable_calls = fixture.sdk->count("set_enable");
  (void)expect_exception<std::logic_error>(
      [&] { fixture.driver->set_enable(2, false); });
  CHECK_EQ(fixture.sdk->count("set_enable"), enable_calls);

  const int no_home_calls = fixture.sdk->count("set_move_no_home");
  (void)expect_exception<std::logic_error>(
      [&] { fixture.driver->set_move_no_home(0); });
  CHECK_EQ(fixture.sdk->count("set_move_no_home"), no_home_calls);
}

void check_dof_mismatch_and_retry(LHandProModel model, int reported_total,
                                  int reported_active, int expected_total,
                                  int expected_active) {
  Fixture fixture(model);
  fixture.sdk->total_dof = reported_total;
  fixture.sdk->active_dof = reported_active;
  CHECK(!fixture.driver->init_hand(false, false, 0.0F));
  CHECK(fixture.released_once());

  int total = -1;
  int active = -1;
  fixture.driver->get_dof(total, active);
  CHECK_EQ(total, 0);
  CHECK_EQ(active, 0);

  fixture.sdk->total_dof = expected_total;
  fixture.sdk->active_dof = expected_active;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->get_dof(total, active);
  CHECK_EQ(total, expected_total);
  CHECK_EQ(active, expected_active);
  fixture.driver->deinit_hand();
  fixture.driver->get_dof(total, active);
  CHECK_EQ(total, 0);
  CHECK_EQ(active, 0);
}

void check_concurrent_dof_snapshot() {
  DofSnapshotProbe probe;
  probe.set_dof(11, 6);

  std::promise<void> first_value_written_promise;
  auto first_value_written = first_value_written_promise.get_future();
  std::promise<void> release_writer_promise;
  auto release_writer = release_writer_promise.get_future().share();
  auto writer = std::async(std::launch::async, [&] {
    probe.set_dof_with_barrier(21, 16, first_value_written_promise,
                               release_writer);
  });
  CHECK(first_value_written.wait_for(2s) == std::future_status::ready);

  auto reader = std::async(std::launch::async, [&] {
    int total = -1;
    int active = -1;
    probe.get_dof(total, active);
    return std::make_pair(total, active);
  });
  CHECK(reader.wait_for(50ms) == std::future_status::timeout);

  release_writer_promise.set_value();
  CHECK(writer.wait_for(2s) == std::future_status::ready);
  writer.get();
  CHECK(reader.wait_for(2s) == std::future_status::ready);
  const auto snapshot = reader.get();
  CHECK_EQ(snapshot.first, 21);
  CHECK_EQ(snapshot.second, 16);
}

void check_constructor_and_home_wait_validation() {
  CHECK(throws_invalid_argument([] {
    make_driver("", LHandProModel::Dof6S, 1);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6S, 0);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6S, 128);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", static_cast<LHandProModel>(99), 1);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6S, 1, nullptr,
                std::make_unique<FakeCanFdTransport>());
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6S, 1,
                std::make_unique<FakeLHandProSdk>(), nullptr);
  }));

  Fixture fixture;
  const std::vector<float> invalid_waits{
      -0.001F, std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()};
  for (float value : invalid_waits) {
    CHECK(!fixture.driver->init_hand(false, false, value));
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
    CHECK_EQ(fixture.sdk->count("create"), 0);
    CHECK_EQ(fixture.transport->open_calls.load(), 0);
    fixture.driver->check_health();
  }
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->deinit_hand();
}

void check_home_wait_returns_on_fresh_completed_feedback() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->int_feedback = 0;
  fixture.sdk->position_feedback = 350;

  auto feedback = std::async(std::launch::async, [&] {
    CHECK(wait_until([&] { return fixture.sdk->count("home_motors") == 1; }));
    std::this_thread::sleep_for(40ms);
    fixture.transport->deliver(realtime_feedback(0xD0U));
    fixture.transport->deliver(realtime_feedback(0xDAU));
  });

  const auto started = std::chrono::steady_clock::now();
  CHECK(fixture.driver->init_hand(false, true, 0.5F));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  CHECK(elapsed < 400ms);
  feedback.get();
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
  fixture.driver->deinit_hand();
}

void check_home_wait_rejects_position_outside_tolerance() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->int_feedback = 0;
  fixture.sdk->position_feedback = 401;

  auto feedback = std::async(std::launch::async, [&] {
    CHECK(wait_until([&] { return fixture.sdk->count("home_motors") == 1; }));
    fixture.transport->deliver(realtime_feedback(0xD0U));
    fixture.transport->deliver(realtime_feedback(0xDAU));
  });

  CHECK(!fixture.driver->init_hand(false, true, 0.08F));
  feedback.get();
  CHECK(fixture.released_once());
  const auto error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(error, "home_motors_timeout", -6, "sync");
}

void check_home_wait_rejects_stale_zero_feedback() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->int_feedback = 0;

  const auto started = std::chrono::steady_clock::now();
  CHECK(!fixture.driver->init_hand(false, true, 0.06F));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  CHECK(elapsed >= 50ms);
  CHECK(fixture.released_once());
  const auto error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(error, "home_motors_timeout", -6, "sync");
}

void check_failure_rollback_and_retry() {
  const std::vector<std::string> failures{
      "create",          "set_hand_type", "get_hand_type",
      "transport.open",  "initial_ex",    "get_dof",
      "set_enable",      "home_motors",   "set_move_no_home"};

  for (const auto& failure : failures) {
    Fixture fixture;
    fixture.fail(failure);
    const bool enable = failure == "set_enable";
    const bool home = failure == "home_motors";
    CHECK(!fixture.driver->init_hand(enable, home, 0.0F));
    CHECK(fixture.released_once());

    int total = -1;
    int active = -1;
    fixture.driver->get_dof(total, active);
    CHECK_EQ(total, 0);
    CHECK_EQ(active, 0);

    CHECK_EQ(fixture.sdk->count("create"), 1);
    if (failure == "create") {
      CHECK_EQ(fixture.sdk->count("destroy"), 0);
      CHECK_EQ(fixture.transport->open_calls.load(), 0);
    } else {
      CHECK_EQ(fixture.sdk->count("destroy"), 1);
    }
    if (failure == "transport.open") {
      CHECK_EQ(fixture.transport->open_calls.load(), 1);
      CHECK_EQ(fixture.transport->close_calls.load(), 0);
    }
    if (failure == "initial_ex" || failure == "get_dof" ||
        failure == "set_enable" || failure == "home_motors" ||
        failure == "set_move_no_home") {
      CHECK_EQ(fixture.transport->clear_calls.load(), 1);
      CHECK_EQ(fixture.transport->close_calls.load(), 1);
      CHECK_EQ(fixture.sdk->count("close"), 1);
      CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
    }

    fixture.clear_failure();
    CHECK(fixture.driver->init_hand(false, false, 0.0F));
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
    const int destroys_before_cleanup = fixture.sdk->count("destroy");
    const int closes_before_cleanup = fixture.transport->close_calls.load();
    fixture.driver->deinit_hand();
    CHECK_EQ(fixture.sdk->count("destroy"), destroys_before_cleanup + 1);
    CHECK_EQ(fixture.transport->close_calls.load(), closes_before_cleanup + 1);
    fixture.driver->deinit_hand();
    CHECK_EQ(fixture.sdk->count("destroy"), destroys_before_cleanup + 1);
    CHECK_EQ(fixture.transport->close_calls.load(), closes_before_cleanup + 1);
  }
}

void check_init_hand_rejects_conflicting_reinit() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);

  // Identical repeat stays idempotent.
  CHECK(fixture.driver->init_hand(false, false, 0.0F));

  // A Ready session keeps its enable/home arguments: a conflicting init
  // must fail explicitly instead of returning success while silently
  // ignoring the new arguments.
  CHECK(!fixture.driver->init_hand(true, false, 0.0F));
  CHECK(!fixture.driver->init_hand(false, true, 0.0F));
  CHECK(!fixture.driver->init_hand(true, true, 5.0F));
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);

  // deinit + re-init with new arguments remains the supported path.
  fixture.driver->deinit_hand();
  CHECK(fixture.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
  fixture.driver->deinit_hand();
}

void check_models_and_initializing_callbacks() {
  Fixture six(LHandProModel::Dof6S);
  six.sdk->during_initial_ex = [&] {
    CHECK_EQ(six.driver->state_for_test(), DriverState::Initializing);
    const unsigned char command[9]{0, 1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(six.sdk->tx_callback != nullptr);
    CHECK(six.sdk->tx_callback(0x601, command, sizeof(command), 0));
    CanFdFrame feedback;
    feedback.id = 0x501;
    feedback.len = 5;
    feedback.data[0] = 0xA5;
    six.transport->deliver(feedback);
  };

  CHECK(six.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(six.driver->state_for_test(), DriverState::Ready);
  six.driver->check_health();
  CHECK_EQ(six.sdk->hand_type, 1);
  CHECK_EQ(six.sdk->count("get_hand_type"), 2);
  CHECK_EQ(six.sdk->last_mode, 1);
  CHECK_EQ(six.sdk->last_node, 1);
  CHECK_EQ(six.sdk->last_decode_size, 5);
  CHECK_EQ(six.transport->open_interface, std::string("can-test"));
  CHECK_EQ(six.transport->open_ids,
           (std::vector<std::uint32_t>{0x501, 0x481, 0x581, 0x181}));

  const auto initial_frames = six.transport->sent_snapshot();
  const auto callback_frame =
      std::find_if(initial_frames.begin(), initial_frames.end(),
                   [](const CanFdFrame& frame) { return frame.id == 0x601U; });
  CHECK(callback_frame != initial_frames.end());
  CHECK(!callback_frame->extended);
  CHECK_EQ(callback_frame->len, 9U);
  CHECK(callback_frame->brs);
  for (std::size_t index = 0; index < 9; ++index) {
    CHECK_EQ(callback_frame->data[index], index);
  }
  CHECK_EQ(count_runtime_feedback_every_base_period_frames(*six.transport, 1U),
           3U);

  int total = 0;
  int active = 0;
  six.driver->get_dof(total, active);
  CHECK_EQ(total, 11);
  CHECK_EQ(active, 6);
  six.sdk->total_dof = 99;
  six.sdk->active_dof = 99;
  six.driver->get_dof(total, active);
  CHECK_EQ(total, 11);
  CHECK_EQ(active, 6);

  const int create_calls = six.sdk->count("create");
  const int open_calls = six.transport->open_calls.load();
  // Re-initializing a Ready driver with different arguments is rejected
  // (the session keeps its enable/home parameters); deinit + init is the
  // supported path and starts a fresh session.
  CHECK(!six.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(six.sdk->count("create"), create_calls);
  CHECK_EQ(six.transport->open_calls.load(), open_calls);
  six.driver->deinit_hand();
  // get_dof cache assertions above poisoned the fake; restore it because
  // the fresh session re-reads DOF from the SDK.
  six.sdk->total_dof = 11;
  six.sdk->active_dof = 6;
  CHECK(six.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(six.sdk->count("create"), create_calls + 1);
  CHECK_EQ(six.transport->open_calls.load(), open_calls + 1);

  auto callback = six.sdk->tx_callback;
  CHECK(callback != nullptr);
  const unsigned char byte{0x5A};
  const auto before_invalid = six.transport->sent_snapshot().size();
  CHECK(!callback(0x601, nullptr, 1, 0));
  CHECK(!callback(0x601, &byte, 65, 0));
  CHECK(!callback(CAN_SFF_MASK + 1U, &byte, 1, 0));
  CHECK(!callback(CAN_EFF_MASK + 1U, &byte, 1, 1));
  CHECK_EQ(six.transport->sent_snapshot().size(), before_invalid);
  six.transport->before_transmit = [] { throw std::runtime_error("blocked"); };
  CHECK(!callback(0x601, &byte, 1, 0));
  CHECK_EQ(six.transport->sent_snapshot().size(), before_invalid);
  six.transport->before_transmit = {};

  six.driver->deinit_hand();
  six.driver->get_dof(total, active);
  CHECK_EQ(total, 0);
  CHECK_EQ(active, 0);

  Fixture sixteen;
  CHECK(sixteen.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(sixteen.sdk->hand_type, 2);
  sixteen.driver->get_dof(total, active);
  CHECK_EQ(total, 21);
  CHECK_EQ(active, 16);
  sixteen.driver->deinit_hand();

  Fixture ordinary_six_dof(LHandProModel::Dof6S);
  ordinary_six_dof.sdk->reported_hand_type_override = 0;
  CHECK(!ordinary_six_dof.driver->init_hand(false, false, 0.0F));
  CHECK(ordinary_six_dof.released_once());
  CHECK_EQ(ordinary_six_dof.transport->open_calls.load(), 0);

  Fixture wrong_second_model(LHandProModel::Dof6S);
  int model_reads = 0;
  wrong_second_model.sdk->before_call = [&](const std::string& operation) {
    if (operation == "get_hand_type" && ++model_reads == 2) {
      wrong_second_model.sdk->reported_hand_type_override = 2;
    }
  };
  CHECK(!wrong_second_model.driver->init_hand(false, false, 0.0F));
  CHECK(wrong_second_model.released_once());
  CHECK_EQ(wrong_second_model.sdk->count("get_hand_type"), 2);

  check_dof_mismatch_and_retry(LHandProModel::Dof6S, 12, 6, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6S, 10, 6, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6S, 11, 5, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6S, 11, 7, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6S, 21, 16, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 22, 16, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 20, 16, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 21, 15, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 21, 17, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 11, 6, 21, 16);
}

void check_runtime_feedback_uses_unit_multiplier_after_every_initial_ex() {
  Fixture motion(LHandProModel::Dof6S, 7);
  int motion_configure_attempts = 0;
  motion.transport->before_transmit = [&] {
    ++motion_configure_attempts;
    CHECK_EQ(motion.sdk->count("initial_ex"), 1);
    CHECK_EQ(motion.sdk->count("get_hand_type"), 1);
    CHECK_EQ(motion.sdk->count("start_monitor"), 0);
    CHECK_EQ(motion.sdk->count("get_dof"), 0);
  };
  CHECK(motion.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(motion_configure_attempts, 3);
  CHECK_EQ(
      count_runtime_feedback_every_base_period_frames(*motion.transport, 7U),
      3U);
  CHECK_EQ(motion.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(motion.sdk->count("save_sdo_drive_param"), 0);
  motion.driver->deinit_hand();

  Fixture provisioning(LHandProModel::Dof6S, 23);
  int provisioning_configure_attempts = 0;
  provisioning.transport->before_transmit = [&] {
    ++provisioning_configure_attempts;
    CHECK_EQ(provisioning.sdk->count("initial_ex"), 1);
    CHECK_EQ(provisioning.sdk->count("get_hand_type"), 1);
    CHECK_EQ(provisioning.sdk->count("start_monitor"), 0);
    CHECK_EQ(provisioning.sdk->count("get_dof"), 0);
  };
  CHECK(provisioning.driver->init_for_provisioning());
  CHECK_EQ(provisioning_configure_attempts, 3);
  CHECK_EQ(count_runtime_feedback_every_base_period_frames(
               *provisioning.transport, 23U),
           3U);
  CHECK_EQ(provisioning.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(provisioning.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(provisioning.sdk->count("save_sdo_drive_param"), 0);
  check_no_motion_state_calls(provisioning);
  provisioning.driver->deinit_hand();
  check_no_motion_state_calls(provisioning);
}

void check_runtime_feedback_transmit_failure_aborts_initialization() {
  for (int failed_attempt = 1; failed_attempt <= 3; ++failed_attempt) {
    Fixture fixture(LHandProModel::Dof6S);
    int attempts = 0;
    fixture.transport->before_transmit = [&] {
      ++attempts;
      fixture.transport->transmit_result = attempts != failed_attempt;
    };

    CHECK(!fixture.driver->init_hand(true, true, 0.0F));
    CHECK_EQ(attempts, failed_attempt);
    CHECK_EQ(count_runtime_feedback_every_base_period_frames(
                 *fixture.transport, 1U),
             static_cast<std::size_t>(failed_attempt));
    CHECK_EQ(fixture.sdk->count("initial_ex"), 1);
    CHECK_EQ(fixture.sdk->count("start_monitor"), 0);
    CHECK_EQ(fixture.sdk->count("get_hand_type"), 1);
    CHECK_EQ(fixture.sdk->count("get_dof"), 0);
    CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
    CHECK_EQ(fixture.sdk->count("home_motors"), 0);
    CHECK_EQ(fixture.sdk->count("move_motors"), 0);
    CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
    CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 0);
    CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 0);
    CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
    check_safety_trio_once(fixture);
    check_fully_released(fixture);
    const auto root = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    check_fault_message(root, "configure_realtime_feedback", -1, "sync");
  }
}

void check_ready_async_decode_faults() {
  Fixture healthy;
  CHECK(healthy.driver->init_hand(false, false, 0.0F));
  const int healthy_decode_calls = healthy.sdk->count("decode_canfd");
  CanFdFrame healthy_feedback;
  healthy_feedback.id = 0x501;
  healthy_feedback.len = 6;
  healthy.transport->deliver(healthy_feedback);
  CHECK_EQ(healthy.sdk->count("decode_canfd"), healthy_decode_calls + 1);
  CHECK_EQ(healthy.driver->state_for_test(), DriverState::Ready);
  healthy.driver->check_health();
  healthy.driver->deinit_hand();

  Fixture faulted;
  CHECK(faulted.driver->init_hand(false, false, 0.0F));
  faulted.sdk->fail_operation = "decode_canfd";
  faulted.sdk->failure_code = 7;
  CanFdFrame failed_feedback;
  failed_feedback.id = 0x501;
  failed_feedback.len = 8;
  const int failed_decode_calls = faulted.sdk->count("decode_canfd");
  faulted.transport->deliver(failed_feedback);
  CHECK_EQ(faulted.sdk->count("decode_canfd"), failed_decode_calls + 1);
  CHECK_EQ(faulted.driver->state_for_test(), DriverState::Faulted);
  const auto first_fault = expect_exception<std::runtime_error>(
      [&] { faulted.driver->check_health(); });
  check_fault_message(first_fault, "decode_canfd", 7, "async");

  faulted.sdk->failure_code = 9;
  faulted.transport->deliver(failed_feedback);
  CHECK_EQ(faulted.sdk->count("decode_canfd"), failed_decode_calls + 2);
  CHECK_EQ(faulted.driver->state_for_test(), DriverState::Faulted);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { faulted.driver->check_health(); }),
           first_fault);
}

void check_decode_exception_is_contained_as_async_fault() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.sdk->throw_decode_exception = true;

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 15;
  fixture.transport->deliver(feedback);

  CHECK_EQ(fixture.sdk->count("decode_canfd"), 1);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", -1, "async");
}

void check_initializing_async_decode_fault_aborts_risky_init() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;
  bool callback_checked = false;
  fixture.sdk->during_initial_ex = [&] {
    CanFdFrame feedback;
    feedback.id = 0x501;
    feedback.len = 4;
    fixture.transport->deliver(feedback);

    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
    const auto callback_fault = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    check_fault_message(callback_fault, "decode_canfd", 7, "async");
    callback_checked = true;
  };

  CHECK(!fixture.driver->init_hand(true, true, 0.0F));
  CHECK(callback_checked);
  CHECK_EQ(count_runtime_feedback_every_base_period_frames(*fixture.transport,
                                                            1U),
           0U);
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 1);
  CHECK_EQ(fixture.sdk->count("start_monitor"), 0);
  CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
  CHECK_EQ(fixture.sdk->count("home_motors"), 0);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");
}

void check_async_fault_during_feedback_window_aborts_before_queries() {
  for (int fault_after_attempt = 1; fault_after_attempt <= 3;
       ++fault_after_attempt) {
    Fixture fixture(LHandProModel::Dof6S);
    fixture.sdk->fail_operation = "decode_canfd";
    int attempts = 0;
    bool delivered = false;
    fixture.transport->after_transmit = [&] {
      ++attempts;
      if (attempts != fault_after_attempt) return;
      delivered = true;
      CanFdFrame feedback;
      feedback.id = 0x501;
      feedback.len = 8;
      fixture.transport->deliver(feedback);
    };

    CHECK(!fixture.driver->init_hand(true, true, 0.0F));
    CHECK(delivered);
    CHECK_EQ(attempts, fault_after_attempt);
    CHECK_EQ(count_runtime_feedback_every_base_period_frames(
                 *fixture.transport, 1U),
             static_cast<std::size_t>(fault_after_attempt));
    CHECK_EQ(fixture.sdk->count("initial_ex"), 1);
    CHECK_EQ(fixture.sdk->count("start_monitor"), 0);
    CHECK_EQ(fixture.sdk->count("get_hand_type"), 1);
    CHECK_EQ(fixture.sdk->count("get_dof"), 0);
    CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
    CHECK_EQ(fixture.sdk->count("home_motors"), 0);
    CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
    check_safety_trio_once(fixture);
    const auto root = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    check_fault_message(root, "decode_canfd", 7, "async");
  }
}

void check_completed_async_fault_blocks_later_risky_init() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> get_dof_entered_promise;
  auto get_dof_entered = get_dof_entered_promise.get_future();
  std::promise<void> release_get_dof_promise;
  auto release_get_dof = release_get_dof_promise.get_future().share();
  std::atomic<bool> get_dof_blocked{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "get_dof" && !get_dof_blocked.exchange(true)) {
      get_dof_entered_promise.set_value();
      release_get_dof.wait();
    }
  };

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(true, true, 0.0F);
  });
  CHECK(get_dof_entered.wait_for(2s) == std::future_status::ready);

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 9;
  auto receive = std::async(std::launch::async,
                            [&] { fixture.transport->deliver(feedback); });
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();
  CHECK(initialization.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");

  release_get_dof_promise.set_value();
  CHECK(initialization.wait_for(2s) == std::future_status::ready);
  CHECK(!initialization.get());
  CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
  CHECK_EQ(fixture.sdk->count("home_motors"), 0);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);
}

void check_admitted_risky_init_serializes_async_fault() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> enable_entered_promise;
  auto enable_entered = enable_entered_promise.get_future();
  std::promise<void> release_enable_promise;
  auto release_enable = release_enable_promise.get_future().share();
  std::atomic<bool> enable_blocked{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_enable" && !enable_blocked.exchange(true)) {
      enable_entered_promise.set_value();
      release_enable.wait();
    }
  };

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(true, true, 0.0F);
  });
  CHECK(enable_entered.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Initializing);

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 10;
  std::promise<void> receive_started_promise;
  auto receive_started = receive_started_promise.get_future();
  auto receive = std::async(std::launch::async, [&] {
    receive_started_promise.set_value();
    fixture.transport->deliver(feedback);
  });
  CHECK(receive_started.wait_for(2s) == std::future_status::ready);
  CHECK(receive.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Initializing);
  fixture.driver->check_health();

  release_enable_promise.set_value();
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");

  CHECK(initialization.wait_for(3s) == std::future_status::ready);
  CHECK(!initialization.get());
  CHECK_EQ(fixture.sdk->count_set_enable(true), 1);
  CHECK_EQ(fixture.sdk->count("home_motors"), 0);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);
}

void check_async_fault_precedes_final_ready_transition() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> home_entered_promise;
  auto home_entered = home_entered_promise.get_future();
  std::promise<void> release_home_promise;
  auto release_home = release_home_promise.get_future().share();
  std::atomic<bool> home_blocked{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "home_motors" && !home_blocked.exchange(true)) {
      home_entered_promise.set_value();
      release_home.wait();
    }
  };

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(false, true, 0.25F);
  });
  CHECK(home_entered.wait_for(2s) == std::future_status::ready);

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 12;
  std::promise<void> receive_started_promise;
  auto receive_started = receive_started_promise.get_future();
  auto receive = std::async(std::launch::async, [&] {
    receive_started_promise.set_value();
    fixture.transport->deliver(feedback);
  });
  CHECK(receive_started.wait_for(2s) == std::future_status::ready);
  CHECK(receive.wait_for(50ms) == std::future_status::timeout);

  release_home_promise.set_value();
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();
  CHECK(wait_until([&] {
    const auto state = fixture.driver->state_for_test();
    return state == DriverState::Faulted || state == DriverState::Created;
  }));
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");

  CHECK(initialization.wait_for(2s) == std::future_status::ready);
  CHECK(!initialization.get());
  CHECK_EQ(fixture.sdk->count("home_motors"), 1);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);
}

void check_rx_queued_during_no_home_decodes_after_release() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> no_home_entered_promise;
  auto no_home_entered = no_home_entered_promise.get_future();
  std::promise<void> release_no_home_promise;
  auto release_no_home = release_no_home_promise.get_future().share();
  std::promise<void> decode_entered_promise;
  auto decode_entered = decode_entered_promise.get_future();
  std::promise<void> registration_entered_promise;
  auto registration_entered = registration_entered_promise.get_future();
  std::atomic<bool> no_home_blocked{false};
  std::atomic<bool> decode_observed{false};
  fixture.driver->set_rx_registered_hook_for_test(
      [&] { registration_entered_promise.set_value(); });
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_move_no_home" &&
        !no_home_blocked.exchange(true)) {
      no_home_entered_promise.set_value();
      release_no_home.wait();
    } else if (operation == "decode_canfd" &&
               !decode_observed.exchange(true)) {
      decode_entered_promise.set_value();
    }
  };

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(false, false, 0.0F);
  });
  CHECK(no_home_entered.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 1);

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 13;
  std::promise<void> receive_started_promise;
  auto receive_started = receive_started_promise.get_future();
  auto receive = std::async(std::launch::async, [&] {
    receive_started_promise.set_value();
    fixture.transport->deliver(feedback);
  });
  CHECK(receive_started.wait_for(2s) == std::future_status::ready);
  CHECK(registration_entered.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(fixture.driver->pending_rx_callbacks_for_test(), 1U);
  CHECK(decode_entered.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 0);
  CHECK(receive.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Initializing);
  fixture.driver->check_health();

  release_no_home_promise.set_value();
  CHECK(decode_entered.wait_for(2s) == std::future_status::ready);
  CHECK(wait_until([&] { return fixture.sdk->count("decode_canfd") == 1; }));
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");

  CHECK(initialization.wait_for(2s) == std::future_status::ready);
  CHECK(!initialization.get());
  CHECK(fixture.driver->state_for_test() != DriverState::Ready);
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 1);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);
}

void check_registered_rx_blocks_final_ready_transition() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> transition_entered_promise;
  auto transition_entered = transition_entered_promise.get_future();
  std::promise<void> release_transition_promise;
  auto release_transition = release_transition_promise.get_future().share();
  std::promise<void> registration_entered_promise;
  auto registration_entered = registration_entered_promise.get_future();
  std::promise<void> release_registration_promise;
  auto release_registration = release_registration_promise.get_future().share();
  fixture.driver->set_ready_transition_hook_for_test([&] {
    transition_entered_promise.set_value();
    release_transition.wait();
  });
  fixture.driver->set_rx_registered_hook_for_test([&] {
    registration_entered_promise.set_value();
    release_registration.wait();
  });

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(false, false, 0.0F);
  });
  CHECK(transition_entered.wait_for(2s) == std::future_status::ready);

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 14;
  auto receive = std::async(std::launch::async,
                            [&] { fixture.transport->deliver(feedback); });
  CHECK(registration_entered.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(fixture.driver->pending_rx_callbacks_for_test(), 1U);

  release_transition_promise.set_value();
  CHECK(initialization.wait_for(50ms) == std::future_status::timeout);
  release_registration_promise.set_value();
  CHECK(initialization.wait_for(2s) == std::future_status::ready);
  const bool initialized = initialization.get();
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();

  CHECK(!initialized);
  CHECK(fixture.driver->state_for_test() != DriverState::Ready);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");
}

void check_final_commit_excludes_late_rx_registration() {
  Fixture fixture;
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 7;

  std::promise<void> final_commit_entered_promise;
  auto final_commit_entered = final_commit_entered_promise.get_future();
  std::promise<void> release_final_commit_promise;
  auto release_final_commit = release_final_commit_promise.get_future().share();
  std::promise<void> entry_attempted_promise;
  auto entry_attempted = entry_attempted_promise.get_future();
  std::promise<void> release_entry_attempt_promise;
  auto release_entry_attempt = release_entry_attempt_promise.get_future().share();
  std::promise<void> registered_promise;
  auto registered = registered_promise.get_future();

  fixture.driver->set_final_commit_hook_for_test([&] {
    final_commit_entered_promise.set_value();
    release_final_commit.wait();
  });
  fixture.driver->set_rx_entry_attempt_hook_for_test([&] {
    entry_attempted_promise.set_value();
    release_entry_attempt.wait();
  });
  fixture.driver->set_rx_registered_hook_for_test(
      [&] { registered_promise.set_value(); });

  auto initialization = std::async(std::launch::async, [&] {
    return fixture.driver->init_hand(false, false, 0.0F);
  });
  CHECK(final_commit_entered.wait_for(2s) == std::future_status::ready);
  CHECK(fixture.driver->rx_entry_registration_locked_for_test());

  CanFdFrame feedback;
  feedback.id = 0x501;
  feedback.len = 16;
  auto receive = std::async(std::launch::async,
                            [&] { fixture.transport->deliver(feedback); });
  CHECK(entry_attempted.wait_for(2s) == std::future_status::ready);
  release_entry_attempt_promise.set_value();
  CHECK(registered.wait_for(50ms) == std::future_status::timeout);

  release_final_commit_promise.set_value();
  CHECK(initialization.wait_for(2s) == std::future_status::ready);
  CHECK(initialization.get());
  CHECK(registered.wait_for(2s) == std::future_status::ready);
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();

  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(root, "decode_canfd", 7, "async");
}

void check_health_contract_and_state_guards() {
  DofSnapshotProbe probe;
  const auto unsupported = expect_exception<std::logic_error>(
      [&] { probe.check_health(); });
  CHECK(unsupported.find("health check unsupported") != std::string::npos);

  Fixture fixture;
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
  fixture.driver->check_health();
  check_not_ready_calls_throw_without_sdk(fixture);

  int total = -1;
  int active = -1;
  fixture.driver->get_dof(total, active);
  CHECK_EQ(total, 0);
  CHECK_EQ(active, 0);
  CHECK_EQ(fixture.driver->get_can_name(), std::string("can-test"));
  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.sdk->count("destroy"), 0);

  bool initializing_checked = false;
  fixture.sdk->during_initial_ex = [&] {
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Initializing);
    fixture.driver->check_health();
    check_not_ready_calls_throw_without_sdk(fixture);
    initializing_checked = true;
  };
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  CHECK(initializing_checked);
  fixture.driver->check_health();

  bool stopping_checked = false;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "stop_monitor") return;
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Stopping);
    fixture.driver->check_health();
    check_not_ready_calls_throw_without_sdk(fixture);
    stopping_checked = true;
  };
  fixture.driver->deinit_hand();
  CHECK(stopping_checked);
  fixture.driver->check_health();
}

void check_ready_sdk_failures_are_faults() {
  for (const auto& call : all_public_sdk_calls()) {
    Fixture fixture;
    CHECK(fixture.driver->init_hand(false, false, 0.0F));
    fixture.driver->check_health();
    const int calls_before = fixture.sdk->count(call.operation);
    fixture.sdk->fail_operation = call.operation;

    const auto call_error =
        expect_exception<std::runtime_error>([&] { call.invoke(fixture); });
    check_fault_message(call_error, call.operation, 7, "sync");
    CHECK_EQ(fixture.sdk->count(call.operation), calls_before + 1);
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);

    const auto health_error = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    CHECK_EQ(health_error, call_error);
  }

  Fixture zero_feedback;
  CHECK(zero_feedback.driver->init_hand(false, false, 0.0F));
  zero_feedback.sdk->int_feedback = 0;
  zero_feedback.sdk->angle_feedback = 0.0F;
  CHECK_EQ(zero_feedback.driver->get_now_position(1), 0);
  CHECK_EQ(zero_feedback.driver->get_now_angle(1), 0.0F);
  CHECK_EQ(zero_feedback.driver->get_now_status(1), 0);
  CHECK_EQ(zero_feedback.driver->get_now_current(1), 0);
  CHECK_EQ(zero_feedback.driver->get_now_alarm(1), 0);
  CHECK_EQ(zero_feedback.driver->state_for_test(), DriverState::Ready);
  zero_feedback.driver->check_health();
}

std::vector<DriverCall> faulted_blocked_calls() {
  auto calls = all_public_sdk_calls();
  calls.erase(std::remove_if(calls.begin(), calls.end(),
                             [](const DriverCall& call) {
                               return call.operation == "stop_motors";
                             }),
              calls.end());
  return calls;
}

void check_faulted_call_policy_and_sticky_fault() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.sdk->fail_operation = "move_motors";
  const auto root = expect_exception<std::runtime_error>(
      [&] { fixture.driver->move_motors(1); });
  check_fault_message(root, "move_motors", 7, "sync");
  fixture.sdk->fail_operation.clear();

  for (const auto& call : faulted_blocked_calls()) {
    const int calls_before = fixture.sdk->count(call.operation);
    const auto error =
        expect_exception<std::runtime_error>([&] { call.invoke(fixture); });
    CHECK_EQ(error, root);
    CHECK_EQ(fixture.sdk->count(call.operation), calls_before);
  }

  const int stop_before = fixture.sdk->count("stop_motors");
  fixture.driver->stop_motors(5);
  CHECK_EQ(fixture.sdk->count("stop_motors"), stop_before + 1);
  CHECK_EQ(fixture.sdk->last_stop_id, 5);

  const int enable_before = fixture.sdk->count("set_enable");
  fixture.driver->set_enable(6, false);
  CHECK_EQ(fixture.sdk->count("set_enable"), enable_before + 1);
  CHECK_EQ(fixture.sdk->last_enable_id, 6);
  CHECK(!fixture.sdk->last_enable);

  const int no_home_before = fixture.sdk->count("set_move_no_home");
  fixture.driver->set_move_no_home(0);
  CHECK_EQ(fixture.sdk->count("set_move_no_home"), no_home_before + 1);
  CHECK_EQ(fixture.sdk->last_move_no_home, 0);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);
  fixture.driver->deinit_hand();

  const std::vector<DriverCall> safety_calls{
      {"stop_motors", [](Fixture& target) {
         target.driver->stop_motors(7);
       }},
      {"set_enable", [](Fixture& target) {
         target.driver->set_enable(7, false);
       }},
      {"set_move_no_home", [](Fixture& target) {
         target.driver->set_move_no_home(0);
       }},
  };
  for (const auto& safety : safety_calls) {
    Fixture target;
    CHECK(target.driver->init_hand(false, false, 0.0F));
    target.sdk->fail_operation = "move_motors";
    const auto first = expect_exception<std::runtime_error>(
        [&] { target.driver->move_motors(1); });
    target.sdk->fail_operation = safety.operation;
    const int calls_before = target.sdk->count(safety.operation);

    const auto safety_error = expect_exception<std::runtime_error>(
        [&] { safety.invoke(target); });
    check_fault_message(safety_error, safety.operation, 7, "sync");
    CHECK_EQ(target.sdk->count(safety.operation), calls_before + 1);
    CHECK_EQ(expect_exception<std::runtime_error>(
                 [&] { target.driver->check_health(); }),
             first);
  }
}

void check_fault_epoch_reset() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.sdk->fail_operation = "get_now_alarm";
  const auto root = expect_exception<std::runtime_error>(
      [&] { (void)fixture.driver->get_now_alarm(1); });
  CHECK(!fixture.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);

  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
  CHECK_EQ(expect_exception<std::runtime_error>(
               [&] { fixture.driver->check_health(); }),
           root);

  bool healthy_during_initialization = false;
  fixture.sdk->during_initial_ex = [&] {
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Initializing);
    fixture.driver->check_health();
    healthy_during_initialization = true;
  };
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  CHECK(healthy_during_initialization);
  fixture.driver->check_health();
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
}

void check_cleanup_threshold_and_late_init_matrix() {
  const std::vector<std::string> early_failures{
      "create", "set_hand_type", "get_hand_type", "transport.open"};
  for (const auto& failure : early_failures) {
    Fixture fixture;
    fixture.fail(failure);
    CHECK(!fixture.driver->init_hand(false, false, 0.0F));
    CHECK_EQ(fixture.sdk->count_stop_motors(0), 0);
    CHECK_EQ(fixture.sdk->count_set_enable(0, false), 0);
    CHECK_EQ(fixture.sdk->count_set_move_no_home(0), 0);
    const int code = failure == "create" || failure == "transport.open" ? -1
                                                                          : 7;
    const auto root = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    check_fault_message(root, failure, code, "sync");
  }

  Fixture model_mismatch;
  model_mismatch.sdk->reported_hand_type_override = 1;
  CHECK(!model_mismatch.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(model_mismatch.sdk->count_stop_motors(0), 0);
  CHECK_EQ(model_mismatch.sdk->count_set_enable(0, false), 0);
  CHECK_EQ(model_mismatch.sdk->count_set_move_no_home(0), 0);
  const auto mismatch_root = expect_exception<std::runtime_error>(
      [&] { model_mismatch.driver->check_health(); });
  check_fault_message(mismatch_root, "get_hand_type", -2, "sync");

  struct LateFailure {
    std::string name;
    std::string operation;
    bool enable;
    bool home;
    std::function<void(FakeLHandProSdk&)> configure;
  };
  const std::vector<LateFailure> late_failures{
      {"initial_ex", "initial_ex", false, false,
       [](FakeLHandProSdk& sdk) { sdk.script_result("initial_ex", 7); }},
      {"second model", "get_hand_type", false, false,
       [](FakeLHandProSdk& sdk) {
         sdk.script_result("get_hand_type", 0);
         sdk.script_result("get_hand_type", 7);
       }},
      {"DOF", "get_dof", false, false,
       [](FakeLHandProSdk& sdk) { sdk.script_result("get_dof", 7); }},
      {"enable", "set_enable", true, false,
       [](FakeLHandProSdk& sdk) {
         sdk.script_result("set_enable:true", 7);
       }},
      {"home after enable", "home_motors", true, true,
       [](FakeLHandProSdk& sdk) { sdk.script_result("home_motors", 7); }},
      {"no-home after enable and home", "set_move_no_home", true, true,
       [](FakeLHandProSdk& sdk) {
         sdk.script_result("set_move_no_home:1", 7);
       }},
  };

  for (const auto& failure : late_failures) {
    Fixture fixture;
    failure.configure(*fixture.sdk);
    int safety_observations = 0;
    fixture.sdk->before_call = [&](const std::string& operation) {
      check_cleanup_resources_are_live(fixture, safety_observations,
                                       operation);
    };

    CHECK(!fixture.driver->init_hand(failure.enable, failure.home, 0.0F));
    CHECK_EQ(fixture.sdk->count("initial_ex"), 1);
    check_safety_trio_once(fixture);
    check_safety_trio_order(fixture);
    CHECK_EQ(safety_observations, 3);
    check_fully_released(fixture);
    const auto root = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    check_fault_message(root, failure.operation, 7, "sync");
  }
}

void check_cleanup_failure_matrix() {
  struct Injection {
    std::string key;
    std::string operation;
    int code;
  };
  struct Scenario {
    std::vector<Injection> injections;
    std::string first_operation;
    int first_code;
  };
  const std::vector<Scenario> scenarios{
      {{{"stop_motors:0", "stop_motors", 31}}, "stop_motors", 31},
      {{{"set_enable:false", "set_enable", 32}}, "set_enable", 32},
      {{{"set_move_no_home:0", "set_move_no_home", 33}},
       "set_move_no_home", 33},
      {{{"stop_motors:0", "stop_motors", 31},
        {"set_enable:false", "set_enable", 32},
        {"set_move_no_home:0", "set_move_no_home", 33}},
       "stop_motors", 31},
  };

  for (const auto& scenario : scenarios) {
    Fixture fixture;
    CHECK(fixture.driver->init_hand(false, false, 0.0F));
    for (const auto& injection : scenario.injections) {
      fixture.sdk->script_result(injection.key, injection.code);
    }

    int safety_observations = 0;
    fixture.sdk->before_call = [&](const std::string& operation) {
      check_cleanup_resources_are_live(fixture, safety_observations,
                                       operation);
    };
    const auto cleanup_error = expect_exception<std::runtime_error>(
        [&] { fixture.driver->deinit_hand(); });
    check_fault_message(cleanup_error, scenario.first_operation,
                        scenario.first_code, "cleanup");
    check_safety_trio_once(fixture);
    check_safety_trio_order(fixture);
    CHECK_EQ(safety_observations, 3);
    check_fully_released(fixture);
    const auto health_error = expect_exception<std::runtime_error>(
        [&] { fixture.driver->check_health(); });
    CHECK(health_error.rfind(cleanup_error, 0) == 0);
    for (const auto& injection : scenario.injections) {
      check_fault_report_entry(health_error, injection.operation,
                               injection.code, "cleanup");
    }

    fixture.driver->deinit_hand();
    CHECK_EQ(safety_observations, 3);
    fixture.driver.reset();
    CHECK_EQ(safety_observations, 3);
  }
}

void check_failed_init_cleanup_preserves_primary() {
  Fixture synchronous;
  synchronous.sdk->script_result("initial_ex", 7);
  synchronous.sdk->script_result("stop_motors:0", 31);
  synchronous.sdk->script_result("set_enable:false", 32);
  synchronous.sdk->script_result("set_move_no_home:0", 33);
  CHECK(!synchronous.driver->init_hand(false, false, 0.0F));
  check_safety_trio_once(synchronous);
  check_fully_released(synchronous);
  const auto synchronous_root = expect_exception<std::runtime_error>(
      [&] { synchronous.driver->check_health(); });
  CHECK(synchronous_root.rfind(
            fault_message("initial_ex", 7, "sync"), 0) == 0);
  check_fault_report_entry(synchronous_root, "initial_ex", 7, "sync");
  check_fault_report_entry(synchronous_root, "stop_motors", 31, "cleanup");
  check_fault_report_entry(synchronous_root, "set_enable", 32, "cleanup");
  check_fault_report_entry(synchronous_root, "set_move_no_home", 33,
                           "cleanup");

  Fixture asynchronous;
  asynchronous.sdk->fail_operation = "decode_canfd";
  asynchronous.sdk->failure_code = 7;
  asynchronous.sdk->script_result("stop_motors:0", 31);
  asynchronous.sdk->script_result("set_enable:false", 32);
  asynchronous.sdk->script_result("set_move_no_home:0", 33);
  asynchronous.sdk->during_initial_ex = [&] {
    CanFdFrame frame;
    frame.id = 0x501;
    frame.len = 5;
    asynchronous.transport->deliver(frame);
  };
  CHECK(!asynchronous.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(asynchronous.sdk->count_set_enable(true), 0);
  CHECK_EQ(asynchronous.sdk->count("home_motors"), 0);
  CHECK_EQ(asynchronous.sdk->count_set_move_no_home(1), 0);
  check_safety_trio_once(asynchronous);
  check_fully_released(asynchronous);
  const auto asynchronous_root = expect_exception<std::runtime_error>(
      [&] { asynchronous.driver->check_health(); });
  CHECK(asynchronous_root.rfind(
            fault_message("decode_canfd", 7, "async"), 0) == 0);
  check_fault_report_entry(asynchronous_root, "decode_canfd", 7, "async");
  check_fault_report_entry(asynchronous_root, "stop_motors", 31, "cleanup");
  check_fault_report_entry(asynchronous_root, "set_enable", 32, "cleanup");
  check_fault_report_entry(asynchronous_root, "set_move_no_home", 33,
                           "cleanup");
}

void check_faulted_deinit_cleanup_and_epoch_reset() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.sdk->fail_operation = "move_motors";
  const auto primary = expect_exception<std::runtime_error>(
      [&] { fixture.driver->move_motors(1); });
  check_fault_message(primary, "move_motors", 7, "sync");
  fixture.sdk->fail_operation.clear();
  fixture.sdk->script_result("set_enable:false", 32);

  const auto cleanup_error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->deinit_hand(); });
  check_fault_message(cleanup_error, "set_enable", 32, "cleanup");
  check_safety_trio_once(fixture);
  check_fully_released(fixture);
  const auto health_error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  CHECK(health_error.rfind(primary, 0) == 0);
  check_fault_report_entry(health_error, "move_motors", 7, "sync");
  check_fault_report_entry(health_error, "set_enable", 32, "cleanup");

  fixture.driver->deinit_hand();
  fixture.sdk->before_call = {};
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->check_health();
  fixture.driver->deinit_hand();
}

void check_destructor_contains_cleanup_failures() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.sdk->script_result("stop_motors:0", 31);
  fixture.sdk->script_result("set_enable:false", 32);
  fixture.sdk->script_result("set_move_no_home:0", 33);
  int safety_observations = 0;
  auto* driver = fixture.driver.get();
  fixture.sdk->before_call = [&](const std::string& operation) {
    check_cleanup_resources_are_live(fixture, safety_observations, operation,
                                     driver);
  };

  fixture.driver.reset();
  CHECK_EQ(safety_observations, 3);
}

void check_public_successes_and_cleanup_order() {
  Fixture fixture;

  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->move_motors(1);
  fixture.driver->stop_motors(1);
  fixture.driver->set_target_position(1, 100);
  fixture.driver->set_target_angle(1, 10.0F);
  fixture.driver->set_position_velocity(1, 20);
  fixture.driver->set_max_current(1, 30);
  fixture.driver->set_enable(1, true);
  fixture.driver->home_motors(1);
  fixture.driver->set_move_no_home(0);
  fixture.driver->clear_alarm(1);
  CHECK_EQ(fixture.sdk->count("move_motors"), 1);
  CHECK_EQ(fixture.sdk->count("stop_motors"), 1);
  CHECK_EQ(fixture.sdk->count("set_target_position"), 1);
  CHECK_EQ(fixture.sdk->count("set_target_angle"), 1);
  CHECK_EQ(fixture.sdk->count("set_position_velocity"), 1);
  CHECK_EQ(fixture.sdk->count("set_max_current"), 1);
  CHECK_EQ(fixture.sdk->count("set_enable"), 1);
  CHECK_EQ(fixture.sdk->count("home_motors"), 1);
  CHECK_EQ(fixture.sdk->count("set_move_no_home"), 2);
  CHECK_EQ(fixture.sdk->count("clear_alarm"), 1);

  CHECK_EQ(fixture.driver->get_now_position(1), fixture.sdk->int_feedback);
  CHECK_EQ(fixture.driver->get_now_angle(1), fixture.sdk->angle_feedback);
  CHECK_EQ(fixture.driver->get_now_status(1), fixture.sdk->int_feedback);
  CHECK_EQ(fixture.driver->get_now_current(1), fixture.sdk->int_feedback);
  CHECK_EQ(fixture.driver->get_now_alarm(1), fixture.sdk->int_feedback);
  fixture.driver->check_health();

  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "stop_monitor") {
      CHECK_EQ(fixture.driver->state_for_test(), DriverState::Stopping);
      CHECK(fixture.transport->is_open());
      CHECK(fixture.transport->callback_active());
    } else if (operation == "close") {
      CHECK_EQ(fixture.transport->clear_calls.load(), 1);
      CHECK(!fixture.transport->callback_active());
      CHECK(fixture.transport->is_open());
      CHECK(fixture.sdk->tx_callback != nullptr);
    } else if (operation == "clear_tx") {
      CHECK(fixture.transport->is_open());
      CHECK_EQ(fixture.transport->close_calls.load(), 0);
    } else if (operation == "destroy") {
      CHECK(!fixture.transport->is_open());
      CHECK_EQ(fixture.transport->close_calls.load(), 1);
      CHECK(fixture.sdk->tx_callback == nullptr);
    }
  };
  fixture.driver->deinit_hand();

  const auto events = fixture.sdk->event_snapshot();
  const auto position = [&](const std::string& operation) {
    return std::find(events.begin(), events.end(), operation) - events.begin();
  };
  CHECK(position("stop_monitor") < position("close"));
  CHECK(position("close") < position("clear_tx"));
  CHECK(position("clear_tx") < position("destroy"));
  CHECK_EQ(fixture.sdk->count("stop_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("close"), 1);
  CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  CHECK_EQ(fixture.transport->clear_calls.load(), 1);
  CHECK_EQ(fixture.transport->close_calls.load(), 1);

  const int stopped_calls = fixture.sdk->count("stop_motors");
  (void)expect_exception<std::logic_error>(
      [&] { fixture.driver->stop_motors(1); });
  CHECK_EQ(fixture.sdk->count("stop_motors"), stopped_calls);
  (void)expect_exception<std::logic_error>(
      [&] { (void)fixture.driver->get_now_position(1); });
  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  CHECK_EQ(fixture.transport->close_calls.load(), 1);
}

void check_rx_drain() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  std::atomic<bool> blocked{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "decode_canfd" && !blocked.exchange(true)) {
      entered_promise.set_value();
      release.wait();
    }
  };

  CanFdFrame frame;
  frame.id = 0x501;
  frame.len = 7;
  auto receive = std::async(std::launch::async,
                            [&] { fixture.transport->deliver(frame); });
  CHECK(entered.wait_for(2s) == std::future_status::ready);

  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(wait_until([&] {
    return fixture.driver->state_for_test() == DriverState::Stopping;
  }));
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("close"), 0);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);

  release_promise.set_value();
  CHECK(receive.wait_for(2s) == std::future_status::ready);
  receive.get();
  CHECK(cleanup.wait_for(2s) == std::future_status::ready);
  cleanup.get();
  CHECK_EQ(fixture.sdk->last_decode_size, 7);
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 1);
}

void check_tx_drain_and_old_callback_silence() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  auto callback = fixture.sdk->tx_callback;
  CHECK(callback != nullptr);

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  std::atomic<bool> blocked{false};
  fixture.transport->before_transmit = [&] {
    if (!blocked.exchange(true)) {
      entered_promise.set_value();
      release.wait();
    }
  };

  const unsigned char data[8]{1, 2, 3, 4, 5, 6, 7, 8};
  auto send = std::async(std::launch::async,
                         [&] { return callback(0x601, data, 8, 0); });
  CHECK(entered.wait_for(2s) == std::future_status::ready);

  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(wait_until([&] { return fixture.sdk->count("clear_tx") == 1; }));
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(fixture.transport->close_calls.load(), 0);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);

  release_promise.set_value();
  CHECK(send.wait_for(2s) == std::future_status::ready);
  CHECK(send.get());
  CHECK(cleanup.wait_for(2s) == std::future_status::ready);
  cleanup.get();
  const auto sent = fixture.transport->sent_snapshot().size();
  CHECK(!callback(0x601, data, 8, 0));
  CHECK_EQ(fixture.transport->sent_snapshot().size(), sent);
}

void check_public_call_drain() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  std::atomic<bool> blocked{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "move_motors" && !blocked.exchange(true)) {
      entered_promise.set_value();
      release.wait();
    }
  };

  auto move = std::async(std::launch::async,
                         [&] { fixture.driver->move_motors(1); });
  CHECK(entered.wait_for(2s) == std::future_status::ready);
  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(wait_until([&] {
    return fixture.driver->state_for_test() == DriverState::Stopping;
  }));
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);

  const int stop_calls = fixture.sdk->count("stop_motors");
  (void)expect_exception<std::logic_error>(
      [&] { fixture.driver->stop_motors(1); });
  CHECK_EQ(fixture.sdk->count("stop_motors"), stop_calls);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);

  release_promise.set_value();
  CHECK(move.wait_for(2s) == std::future_status::ready);
  move.get();
  CHECK(cleanup.wait_for(2s) == std::future_status::ready);
  cleanup.get();
}

void check_early_stopping_rx_feedback() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  const int decode_calls = fixture.sdk->count("decode_canfd");
  std::atomic<bool> delivered{false};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "stop_monitor" || delivered.exchange(true)) return;
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Stopping);
    CHECK(fixture.transport->callback_active());
    CanFdFrame feedback;
    feedback.id = 0x481;
    feedback.len = 11;
    feedback.data[0] = 0x42;
    fixture.transport->deliver(feedback);
  };

  fixture.driver->deinit_hand();
  CHECK(delivered.load());
  CHECK_EQ(fixture.sdk->count("decode_canfd"), decode_calls + 1);
  CHECK_EQ(fixture.sdk->last_decode_size, 11);
  CHECK_EQ(fixture.transport->clear_calls.load(), 1);
}

void check_single_active_instance() {
  Fixture first;
  Fixture second;
  std::promise<void> start_promise;
  auto start = start_promise.get_future().share();
  auto first_init = std::async(std::launch::async, [&] {
    start.wait();
    return first.driver->init_hand(false, false, 0.0F);
  });
  auto second_init = std::async(std::launch::async, [&] {
    start.wait();
    return second.driver->init_hand(false, false, 0.0F);
  });
  start_promise.set_value();

  const bool first_won = first_init.get();
  const bool second_won = second_init.get();
  CHECK(first_won != second_won);
  Fixture* winner = first_won ? &first : &second;
  Fixture* loser = first_won ? &second : &first;
  CHECK_EQ(winner->driver->state_for_test(), DriverState::Ready);
  CHECK_EQ(loser->driver->state_for_test(), DriverState::Created);
  CHECK_EQ(loser->sdk->count("create"), 0);
  CHECK_EQ(loser->transport->open_calls.load(), 0);
  CHECK_EQ(loser->sdk->count_stop_motors(0), 0);
  CHECK_EQ(loser->sdk->count_set_enable(0, false), 0);
  CHECK_EQ(loser->sdk->count_set_move_no_home(0), 0);
  const auto loser_root = expect_exception<std::runtime_error>(
      [&] { loser->driver->check_health(); });
  check_fault_message(loser_root, "claim_process_slot", -1, "sync");

  winner->driver->deinit_hand();
  CHECK(loser->driver->init_hand(false, false, 0.0F));
  loser->driver->deinit_hand();
}

void check_slot_release_after_full_cleanup() {
  Fixture winner;
  Fixture contender;
  CHECK(winner.driver->init_hand(false, false, 0.0F));
  auto callback = winner.sdk->tx_callback;
  CHECK(callback != nullptr);

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  std::atomic<bool> blocked{false};
  winner.transport->before_transmit = [&] {
    if (!blocked.exchange(true)) {
      entered_promise.set_value();
      release.wait();
    }
  };

  std::promise<void> close_entered_promise;
  auto close_entered = close_entered_promise.get_future();
  std::promise<void> close_release_promise;
  auto close_release = close_release_promise.get_future().share();
  std::atomic<bool> close_blocked{false};
  winner.transport->before_close = [&] {
    if (!close_blocked.exchange(true)) {
      close_entered_promise.set_value();
      close_release.wait();
    }
  };

  std::promise<void> destroy_entered_promise;
  auto destroy_entered = destroy_entered_promise.get_future();
  std::promise<void> destroy_release_promise;
  auto destroy_release = destroy_release_promise.get_future().share();
  std::atomic<bool> destroy_blocked{false};
  winner.sdk->before_call = [&](const std::string& operation) {
    if (operation == "destroy" && !destroy_blocked.exchange(true)) {
      destroy_entered_promise.set_value();
      destroy_release.wait();
    }
  };

  const auto expect_contender_rejected = [&] {
    auto attempt = std::async(std::launch::async, [&] {
      return contender.driver->init_hand(false, false, 0.0F);
    });
    CHECK(attempt.wait_for(200ms) == std::future_status::ready);
    CHECK(!attempt.get());
    CHECK_EQ(contender.driver->state_for_test(), DriverState::Created);
    CHECK_EQ(contender.sdk->count("create"), 0);
    CHECK_EQ(contender.transport->open_calls.load(), 0);
  };

  const unsigned char data[9]{};
  auto send = std::async(std::launch::async,
                         [&] { return callback(0x601, data, 9, 0); });
  CHECK(entered.wait_for(2s) == std::future_status::ready);
  auto cleanup = std::async(std::launch::async,
                            [&] { winner.driver->deinit_hand(); });
  CHECK(wait_until([&] { return winner.sdk->count("clear_tx") == 1; }));
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(winner.transport->close_calls.load(), 0);
  CHECK_EQ(winner.sdk->count("destroy"), 0);

  expect_contender_rejected();

  release_promise.set_value();
  CHECK(send.wait_for(2s) == std::future_status::ready);
  CHECK(send.get());
  CHECK(close_entered.wait_for(2s) == std::future_status::ready);
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(winner.transport->close_calls.load(), 0);
  CHECK_EQ(winner.sdk->count("destroy"), 0);
  expect_contender_rejected();

  close_release_promise.set_value();
  CHECK(destroy_entered.wait_for(2s) == std::future_status::ready);
  CHECK(cleanup.wait_for(50ms) == std::future_status::timeout);
  CHECK_EQ(winner.transport->close_calls.load(), 1);
  CHECK_EQ(winner.sdk->count("destroy"), 0);
  expect_contender_rejected();

  destroy_release_promise.set_value();
  CHECK(cleanup.wait_for(2s) == std::future_status::ready);
  cleanup.get();
  CHECK_EQ(winner.transport->close_calls.load(), 1);
  CHECK_EQ(winner.sdk->count("destroy"), 1);

  CHECK(contender.driver->init_hand(false, false, 0.0F));
  contender.driver->deinit_hand();
}

void check_provisioning_show_and_cleanup_without_motion() {
  Fixture fixture(LHandProModel::Dof6S);

  CHECK(fixture.driver->init_for_provisioning());
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
  CHECK_EQ(fixture.sdk->count("create"), 1);
  CHECK_EQ(fixture.sdk->count("set_hand_type"), 1);
  CHECK_EQ(fixture.sdk->hand_type, 1);
  CHECK_EQ(fixture.sdk->count("get_hand_type"), 2);
  CHECK_EQ(fixture.transport->open_calls.load(), 1);
  CHECK_EQ(fixture.transport->open_interface, std::string("can-test"));
  CHECK_EQ(fixture.transport->open_ids,
           (std::vector<std::uint32_t>{0x501, 0x481, 0x581, 0x181}));
  CHECK(fixture.transport->callback_active());
  CHECK_EQ(fixture.sdk->count("install_tx"), 1);
  CHECK_EQ(fixture.sdk->count("initial_ex"), 1);
  CHECK_EQ(fixture.sdk->last_mode, 1);
  CHECK_EQ(fixture.sdk->last_node, 1);
  CHECK_EQ(fixture.sdk->count("start_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("get_dof"), 1);
  int total = 0;
  int active = 0;
  fixture.driver->get_dof(total, active);
  CHECK_EQ(total, 11);
  CHECK_EQ(active, 6);
  check_no_motion_state_calls(fixture);

  CHECK(fixture.driver->init_for_provisioning());
  CHECK_EQ(fixture.sdk->count("create"), 1);
  CHECK_EQ(fixture.sdk->count("set_hand_type"), 1);
  CHECK_EQ(fixture.sdk->count("get_hand_type"), 2);
  CHECK_EQ(fixture.transport->open_calls.load(), 1);
  CHECK_EQ(fixture.sdk->count("install_tx"), 1);
  CHECK_EQ(fixture.sdk->count("initial_ex"), 1);
  CHECK_EQ(fixture.sdk->count("start_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("get_dof"), 1);
  check_no_motion_state_calls(fixture);

  const auto report = fixture.driver->show_feedback_period();
  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::Shown);
  CHECK(report.success());
  CHECK_EQ(report.before_count, kFeedbackPeriodIndexes.size());
  CHECK_EQ(report.after_count, kFeedbackPeriodIndexes.size());
  const auto reads = fixture.sdk->sdo_read_snapshot();
  CHECK_EQ(reads.size(), kFeedbackPeriodIndexes.size());
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    CHECK_EQ(reads[axis].index, kFeedbackPeriodIndexes[axis]);
    CHECK_EQ(reads[axis].subindex, kFeedbackPeriodSubindex);
  }

  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.sdk->count("stop_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("close"), 1);
  CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  CHECK_EQ(fixture.transport->clear_calls.load(), 1);
  CHECK_EQ(fixture.transport->close_calls.load(), 1);
  check_no_motion_state_calls(fixture);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);

  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.sdk->count("stop_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("close"), 1);
  CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  CHECK_EQ(fixture.transport->clear_calls.load(), 1);
  CHECK_EQ(fixture.transport->close_calls.load(), 1);
  check_no_motion_state_calls(fixture);
}

void check_ready_sessions_reject_cross_purpose_initialization() {
  Fixture motion(LHandProModel::Dof6S);
  CHECK(motion.driver->init_hand(false, false, 0.0F));
  CHECK(!motion.driver->init_for_provisioning());
  CHECK_EQ(motion.driver->state_for_test(), DriverState::Ready);
  motion.driver->move_motors(1);
  CHECK_EQ(motion.sdk->count("move_motors"), 1);
  (void)expect_exception<std::logic_error>(
      [&] { (void)motion.driver->show_feedback_period(); });
  (void)expect_exception<std::logic_error>(
      [&] { (void)motion.driver->apply_feedback_period_20ms(); });
  CHECK_EQ(motion.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(motion.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(motion.sdk->count("save_sdo_drive_param"), 0);
  motion.driver->deinit_hand();
  check_safety_trio_once(motion);

  Fixture provisioning(LHandProModel::Dof6S);
  CHECK(provisioning.driver->init_for_provisioning());
  CHECK(!provisioning.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(provisioning.driver->state_for_test(), DriverState::Ready);
  check_no_motion_state_calls(provisioning);
  const auto report = provisioning.driver->show_feedback_period();
  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::Shown);
  CHECK_EQ(provisioning.sdk->count("get_sdo_drive_param"), 6);
  provisioning.driver->deinit_hand();
  check_no_motion_state_calls(provisioning);
}

void check_provisioning_apply_and_fresh_session() {
  Fixture compliant(LHandProModel::Dof6S);
  CHECK(compliant.driver->init_for_provisioning());
  const auto compliant_report =
      compliant.driver->apply_feedback_period_20ms();
  CHECK_EQ(compliant_report.outcome,
           FeedbackPeriodOutcome::AlreadyCompliant);
  CHECK_EQ(compliant.sdk->count("get_sdo_drive_param"), 6);
  CHECK_EQ(compliant.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(compliant.sdk->count("save_sdo_drive_param"), 0);
  compliant.driver->deinit_hand();
  check_no_motion_state_calls(compliant);

  Fixture changed(LHandProModel::Dof6S);
  changed.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(changed.driver->init_for_provisioning());
  install_early_sdo_acknowledgements(changed);
  const auto changed_report = changed.driver->apply_feedback_period_20ms();
  CHECK_EQ(changed_report.outcome, FeedbackPeriodOutcome::Saved);
  CHECK(changed_report.success());
  CHECK_EQ(changed_report.before.front(), 100U);
  CHECK(std::all_of(changed_report.after.begin(), changed_report.after.end(),
                    [](unsigned int value) {
                      return value == kFeedbackPeriod20msUnits;
                    }));
  CHECK_EQ(changed.sdk->count("get_sdo_drive_param"), 12);
  CHECK_EQ(changed.sdk->count("set_sdo_drive_param"), 6);
  CHECK_EQ(changed.sdk->count("save_sdo_drive_param"), 1);
  changed.driver->deinit_hand();
  check_no_motion_state_calls(changed);

  CHECK(changed.driver->init_for_provisioning());
  CHECK_EQ(changed.driver->state_for_test(), DriverState::Ready);
  const auto fresh_report = changed.driver->show_feedback_period();
  CHECK_EQ(fresh_report.outcome, FeedbackPeriodOutcome::Shown);
  CHECK(std::all_of(fresh_report.before.begin(), fresh_report.before.end(),
                    [](unsigned int value) {
                      return value == kFeedbackPeriod20msUnits;
                    }));
  changed.driver->deinit_hand();
  CHECK_EQ(changed.sdk->count("stop_monitor"), 2);
  CHECK_EQ(changed.sdk->count("close"), 2);
  CHECK_EQ(changed.sdk->count("clear_tx"), 2);
  CHECK_EQ(changed.sdk->count("destroy"), 2);
  CHECK_EQ(changed.transport->clear_calls.load(), 2);
  CHECK_EQ(changed.transport->close_calls.load(), 2);
  check_no_motion_state_calls(changed);
}

void check_provisioning_sdo_acks_are_consumed_and_may_arrive_early() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 3;
  install_early_sdo_acknowledgements(fixture);

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::Saved);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 6);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 1);
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 7);

  CanFdFrame ordinary_feedback;
  ordinary_feedback.id = 0x501U;
  ordinary_feedback.len = 8;
  fixture.transport->deliver(ordinary_feedback);
  const auto error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(error, "decode_canfd", 3, "async");
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 8);
  fixture.sdk->before_call = {};
  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
}

void check_provisioning_waits_for_each_exact_sdo_ack() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());

  auto apply = std::async(std::launch::async, [&] {
    return fixture.driver->apply_feedback_period_20ms();
  });
  CHECK(wait_until(
      [&] { return fixture.sdk->count("set_sdo_drive_param") == 1; }));
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);

  fixture.transport->deliver(
      sdo_response(0x60U, kFeedbackPeriodIndexes[1],
                   kFeedbackPeriodSubindex));
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 1);

  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    fixture.transport->deliver(
        sdo_response(0x60U, kFeedbackPeriodIndexes[axis],
                     kFeedbackPeriodSubindex));
    if (axis + 1 < kFeedbackPeriodIndexes.size()) {
      CHECK(wait_until([&] {
        return fixture.sdk->count("set_sdo_drive_param") ==
               static_cast<int>(axis + 2);
      }));
      CHECK(apply.wait_for(20ms) == std::future_status::timeout);
    }
  }

  CHECK(wait_until(
      [&] { return fixture.sdk->count("save_sdo_drive_param") == 1; }));
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);
  fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));
  CHECK(apply.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(apply.get().outcome, FeedbackPeriodOutcome::Saved);
  fixture.driver->deinit_hand();
}

void check_provisioning_rejects_malformed_sdo_ack_lengths() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param" &&
        fixture.sdk->sdo_write_attempt_snapshot().size() > 1U) {
      deliver_latest_write_response(fixture, 0x60U);
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));
    }
  };

  auto apply = std::async(std::launch::async, [&] {
    return fixture.driver->apply_feedback_period_20ms();
  });
  CHECK(wait_until(
      [&] { return fixture.sdk->count("set_sdo_drive_param") == 1; }));

  for (const std::uint8_t length : {4U, 5U, 6U, 7U, 9U, 16U, 64U}) {
    auto malformed =
        sdo_response(0x60U, kFeedbackPeriodIndexes.front(),
                     kFeedbackPeriodSubindex);
    malformed.len = length;
    fixture.transport->deliver(malformed);
  }
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 1);

  fixture.transport->deliver(
      sdo_response(0x60U, kFeedbackPeriodIndexes.front(),
                   kFeedbackPeriodSubindex));
  CHECK(apply.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(apply.get().outcome, FeedbackPeriodOutcome::Saved);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_waits_for_ack_callback_completion() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  std::promise<void> decode_entered_promise;
  auto decode_entered = decode_entered_promise.get_future();
  std::promise<void> release_decode_promise;
  auto release_decode = release_decode_promise.get_future().share();
  std::atomic<bool> block_first_decode{true};
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      if (fixture.sdk->sdo_write_attempt_snapshot().size() > 1U) {
        deliver_latest_write_response(fixture, 0x60U);
      }
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));
    } else if (operation == "decode_canfd" &&
               block_first_decode.exchange(false)) {
      decode_entered_promise.set_value();
      release_decode.wait();
    }
  };

  auto apply = std::async(std::launch::async, [&] {
    return fixture.driver->apply_feedback_period_20ms();
  });
  CHECK(wait_until(
      [&] { return fixture.sdk->count("set_sdo_drive_param") == 1; }));
  auto ack = std::async(std::launch::async, [&] {
    fixture.transport->deliver(
        sdo_response(0x60U, kFeedbackPeriodIndexes.front(),
                     kFeedbackPeriodSubindex));
  });
  CHECK(decode_entered.wait_for(2s) == std::future_status::ready);
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 1);

  release_decode_promise.set_value();
  CHECK(ack.wait_for(2s) == std::future_status::ready);
  ack.get();
  CHECK(apply.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(apply.get().outcome, FeedbackPeriodOutcome::Saved);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 6);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_sdo_abort_rolls_back_all_axes() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  int writes = 0;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "set_sdo_drive_param") return;
    ++writes;
    deliver_latest_write_response(fixture, writes == 2 ? 0x80U : 0x60U);
  };

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::FailedRestored);
  CHECK_EQ(report.failure.operation, std::string("set_sdo_drive_param"));
  CHECK_EQ(report.failure.axis, 2U);
  CHECK(report.failure.code != 0);
  CHECK(report.rollback_attempted);
  CHECK(report.rollback_verified);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 8);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_sdo_ack_timeout_rolls_back() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  int writes = 0;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "set_sdo_drive_param") return;
    if (++writes > 1) deliver_latest_write_response(fixture, 0x60U);
  };

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::FailedRestored);
  CHECK_EQ(report.failure.operation, std::string("set_sdo_drive_param"));
  CHECK_EQ(report.failure.axis, 1U);
  CHECK(report.failure.code != 0);
  CHECK(report.rollback_attempted);
  CHECK(report.rollback_verified);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 7);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_sync_write_failure_cancels_ack_wait() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  fixture.sdk->script_result("set_sdo_drive_param", 71);
  CHECK(fixture.driver->init_for_provisioning());
  int writes = 0;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "set_sdo_drive_param") return;
    if (++writes > 1) deliver_latest_write_response(fixture, 0x60U);
  };

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::FailedRestored);
  CHECK_EQ(report.failure.operation, std::string("set_sdo_drive_param"));
  CHECK_EQ(report.failure.code, 71);
  CHECK_EQ(report.failure.axis, 1U);
  CHECK(report.rollback_verified);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 7);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_save_requires_ack() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      deliver_latest_write_response(fixture, 0x60U);
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(sdo_response(0x80U, 0x1010U, 0x01U));
    }
  };

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::SaveFailed);
  CHECK_EQ(report.failure.operation, std::string("save_sdo_drive_param"));
  CHECK(report.failure.code != 0);
  CHECK(report.save_attempted);
  CHECK(!report.success());
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_wrong_save_ack_times_out() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      deliver_latest_write_response(fixture, 0x60U);
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(sdo_response(0x60U, 0x1011U, 0x01U));
    }
  };

  const auto report = fixture.driver->apply_feedback_period_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::SaveFailed);
  CHECK_EQ(report.failure.operation, std::string("save_sdo_drive_param"));
  CHECK(report.failure.code != 0);
  CHECK(report.save_attempted);
  CHECK(!report.success());
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
}

void check_provisioning_save_probe_does_not_complete_ack_wait() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 3;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      deliver_latest_write_response(fixture, 0x60U);
    } else if (operation == "save_sdo_drive_param") {
      fixture.transport->deliver(save_compatibility_probe_response());
    }
  };

  auto apply = std::async(std::launch::async, [&] {
    try {
      const auto report = fixture.driver->apply_feedback_period_20ms();
      return std::string(feedback_period_outcome_name(report.outcome));
    } catch (const std::exception& error) {
      return std::string(error.what());
    }
  });
  CHECK(wait_until(
      [&] { return fixture.sdk->count("save_sdo_drive_param") == 1; }));
  CHECK(apply.wait_for(20ms) == std::future_status::timeout);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 7);

  fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));
  CHECK(apply.wait_for(2s) == std::future_status::ready);
  CHECK_EQ(apply.get(), std::string("saved"));
  CHECK_EQ(fixture.sdk->count("decode_canfd"), 8);
  fixture.sdk->before_call = {};
  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
}

void check_save_probe_outside_pending_save_remains_fail_closed() {
  Fixture fixture(LHandProModel::Dof6S);
  CHECK(fixture.driver->init_for_provisioning());
  fixture.sdk->fail_operation = "decode_canfd";
  fixture.sdk->failure_code = 3;

  fixture.transport->deliver(save_compatibility_probe_response());

  const auto error = expect_exception<std::runtime_error>(
      [&] { fixture.driver->check_health(); });
  check_fault_message(error, "decode_canfd", 3, "async");
  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
}

void check_malformed_save_probes_remain_fail_closed() {
  std::vector<CanFdFrame> malformed;
  auto append = [&](const std::function<void(CanFdFrame&)>& mutate) {
    auto frame = save_compatibility_probe_response();
    mutate(frame);
    malformed.push_back(frame);
  };
  append([](CanFdFrame& frame) { frame.id = 0x582U; });
  append([](CanFdFrame& frame) { frame.extended = true; });
  append([](CanFdFrame& frame) { frame.len = 7U; });
  append([](CanFdFrame& frame) { frame.len = 9U; });
  append([](CanFdFrame& frame) { frame.data[0] = 0x01U; });
  append([](CanFdFrame& frame) { frame.data[1] = 0x11U; });
  append([](CanFdFrame& frame) { frame.data[3] = 0x01U; });
  append([](CanFdFrame& frame) { frame.data[4] = 0x21U; });

  for (const auto& malformed_probe : malformed) {
    Fixture fixture(LHandProModel::Dof6S);
    fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
    CHECK(fixture.driver->init_for_provisioning());
    fixture.sdk->fail_operation = "decode_canfd";
    fixture.sdk->failure_code = 3;
    fixture.sdk->before_call = [&](const std::string& operation) {
      if (operation == "set_sdo_drive_param") {
        deliver_latest_write_response(fixture, 0x60U);
      }
    };

    auto apply = std::async(std::launch::async, [&] {
      return expect_exception<std::runtime_error>(
          [&] { (void)fixture.driver->apply_feedback_period_20ms(); });
    });
    CHECK(wait_until(
        [&] { return fixture.sdk->count("save_sdo_drive_param") == 1; }));
    fixture.transport->deliver(malformed_probe);
    fixture.transport->deliver(sdo_response(0x60U, 0x1010U, 0x01U));

    CHECK(apply.wait_for(2s) == std::future_status::ready);
    check_fault_message(apply.get(), "decode_canfd", 3, "async");
    fixture.sdk->before_call = {};
    fixture.sdk->fail_operation.clear();
    fixture.driver->deinit_hand();
  }
}

void check_cleanup_cancels_pending_sdo_ack_wait() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());

  auto apply = std::async(std::launch::async, [&] {
    return expect_exception<std::logic_error>(
        [&] { (void)fixture.driver->apply_feedback_period_20ms(); });
  });
  CHECK(wait_until(
      [&] { return fixture.sdk->count("set_sdo_drive_param") == 1; }));
  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });

  CHECK(cleanup.wait_for(2s) == std::future_status::ready);
  cleanup.get();
  CHECK(apply.wait_for(2s) == std::future_status::ready);
  CHECK(apply.get().find("requires Ready state") != std::string::npos);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
}

void check_provisioning_state_purpose_and_model_guards() {
  Fixture created(LHandProModel::Dof6S);
  (void)expect_exception<std::logic_error>(
      [&] { (void)created.driver->show_feedback_period(); });
  (void)expect_exception<std::logic_error>(
      [&] { (void)created.driver->apply_feedback_period_20ms(); });
  CHECK_EQ(created.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(created.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(created.sdk->count("save_sdo_drive_param"), 0);

  Fixture motion(LHandProModel::Dof6S);
  CHECK(motion.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(motion.sdk->count_set_move_no_home(1), 1);
  const auto show_error = expect_exception<std::logic_error>(
      [&] { (void)motion.driver->show_feedback_period(); });
  const auto apply_error = expect_exception<std::logic_error>(
      [&] { (void)motion.driver->apply_feedback_period_20ms(); });
  CHECK(show_error.find("provisioning session") != std::string::npos);
  CHECK(apply_error.find("provisioning session") != std::string::npos);
  CHECK_EQ(motion.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(motion.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(motion.sdk->count("save_sdo_drive_param"), 0);
  motion.driver->deinit_hand();
  CHECK_EQ(motion.sdk->count_set_move_no_home(1), 1);
  check_safety_trio_once(motion);

  Fixture dof16(LHandProModel::Dof16);
  CHECK(!dof16.driver->init_for_provisioning());
  CHECK_EQ(dof16.driver->state_for_test(), DriverState::Created);
  CHECK_EQ(dof16.sdk->count("create"), 0);
  CHECK_EQ(dof16.sdk->count("set_hand_type"), 0);
  CHECK_EQ(dof16.transport->open_calls.load(), 0);
  dof16.driver->deinit_hand();
  CHECK_EQ(dof16.sdk->count("destroy"), 0);
  check_no_motion_state_calls(dof16);
}

void check_provisioning_initialization_failure_matrix() {
  struct Scenario {
    const char* operation;
    std::function<void(Fixture&)> inject;
  };
  const std::vector<Scenario> scenarios{
      {"create", [](Fixture& fixture) { fixture.fail("create"); }},
      {"set_hand_type",
       [](Fixture& fixture) { fixture.fail("set_hand_type"); }},
      {"get_hand_type",
       [](Fixture& fixture) { fixture.fail("get_hand_type"); }},
      {"transport.open",
       [](Fixture& fixture) { fixture.fail("transport.open"); }},
      {"initial_ex", [](Fixture& fixture) { fixture.fail("initial_ex"); }},
      {"get_dof", [](Fixture& fixture) { fixture.fail("get_dof"); }},
      {"decode_canfd",
       [](Fixture& fixture) {
         fixture.fail("decode_canfd");
         fixture.sdk->during_initial_ex = [&fixture] {
           CanFdFrame frame;
           frame.id = 0x581;
           frame.len = 8;
           fixture.transport->deliver(frame);
         };
       }},
  };

  for (const auto& scenario : scenarios) {
    Fixture fixture(LHandProModel::Dof6S);
    scenario.inject(fixture);
    CHECK(!fixture.driver->init_for_provisioning());
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
    CHECK(!fixture.sdk->created);
    CHECK(!fixture.sdk->monitor_started);
    CHECK(fixture.sdk->tx_callback == nullptr);
    CHECK(!fixture.transport->is_open());
    CHECK(!fixture.transport->callback_active());
    CHECK_EQ(fixture.sdk->count("destroy"),
             std::string(scenario.operation) == "create" ? 0 : 1);
    if (std::string(scenario.operation) == "initial_ex" ||
        std::string(scenario.operation) == "get_dof" ||
        std::string(scenario.operation) == "decode_canfd") {
      CHECK_EQ(fixture.sdk->count("close"), 1);
      CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
      CHECK_EQ(fixture.transport->clear_calls.load(), 1);
      CHECK_EQ(fixture.transport->close_calls.load(), 1);
    }
    check_no_motion_state_calls(fixture);

    fixture.clear_failure();
    fixture.sdk->during_initial_ex = {};
    CHECK(fixture.driver->init_for_provisioning());
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
    fixture.driver->deinit_hand();
    CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
    check_no_motion_state_calls(fixture);
  }
}

void check_provisioning_show_surfaces_async_fault() {
  Fixture fixture(LHandProModel::Dof6S);
  CHECK(fixture.driver->init_for_provisioning());

  fixture.sdk->fail_operation = "decode_canfd";
  bool delivered = false;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "get_sdo_drive_param" || delivered) return;
    delivered = true;
    CanFdFrame frame;
    frame.id = 0x581;
    frame.len = 8;
    fixture.transport->deliver(frame);
  };

  const auto error = expect_exception<std::runtime_error>(
      [&] { (void)fixture.driver->show_feedback_period(); });
  CHECK(delivered);
  check_fault_message(error, "decode_canfd", 7, "async");
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Faulted);
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 6);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  check_no_motion_state_calls(fixture);

  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Created);
  CHECK_EQ(fixture.sdk->count("stop_monitor"), 1);
  CHECK_EQ(fixture.sdk->count("close"), 1);
  CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
  CHECK_EQ(fixture.sdk->count("destroy"), 1);
  check_no_motion_state_calls(fixture);
}

void check_provisioning_call_revalidates_replacement_session() {
  Fixture fixture(LHandProModel::Dof6S);
  CHECK(fixture.driver->init_for_provisioning());

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  fixture.driver->set_provisioning_pre_lock_hook_for_test([&] {
    entered_promise.set_value();
    release.wait();
  });

  auto config_call = std::async(std::launch::async, [&] {
    return expect_exception<std::logic_error>(
        [&] { (void)fixture.driver->show_feedback_period(); });
  });
  PromiseReleaseGuard release_guard(release_promise);
  CHECK(entered.wait_for(2s) == std::future_status::ready);

  fixture.driver->deinit_hand();
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Ready);
  release_guard.release();
  CHECK(config_call.wait_for(2s) == std::future_status::ready);
  const auto error = config_call.get();
  CHECK(error.find("provisioning session") != std::string::npos);
  fixture.driver->set_provisioning_pre_lock_hook_for_test({});

  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.driver->deinit_hand();
  CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 1);
  check_safety_trio_once(fixture);
}

void check_provisioning_call_rejects_same_purpose_replacement() {
  Fixture fixture(LHandProModel::Dof6S);
  CHECK(fixture.driver->init_for_provisioning());

  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  fixture.driver->set_provisioning_pre_lock_hook_for_test([&] {
    entered_promise.set_value();
    release.wait();
  });

  auto old_config_call = std::async(std::launch::async, [&] {
    return expect_exception<std::logic_error>(
        [&] { (void)fixture.driver->show_feedback_period(); });
  });
  PromiseReleaseGuard release_guard(release_promise);
  CHECK(entered.wait_for(2s) == std::future_status::ready);

  fixture.driver->deinit_hand();
  CHECK(fixture.driver->init_for_provisioning());
  release_guard.release();
  CHECK(old_config_call.wait_for(2s) == std::future_status::ready);
  const auto error = old_config_call.get();
  CHECK(error.find("session changed") != std::string::npos);
  fixture.driver->set_provisioning_pre_lock_hook_for_test({});

  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 0);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  const auto replacement_report = fixture.driver->show_feedback_period();
  CHECK_EQ(replacement_report.outcome, FeedbackPeriodOutcome::Shown);
  fixture.driver->deinit_hand();
  check_no_motion_state_calls(fixture);
}

void check_async_fault_before_apply_writes_cancels_transaction() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());

  fixture.sdk->fail_operation = "decode_canfd";
  bool delivered = false;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "get_sdo_drive_param" || delivered) return;
    delivered = true;
    CanFdFrame frame;
    frame.id = 0x581;
    frame.len = 8;
    fixture.transport->deliver(frame);
  };

  const auto error = expect_exception<std::runtime_error>(
      [&] { (void)fixture.driver->apply_feedback_period_20ms(); });
  check_fault_message(error, "decode_canfd", 7, "async");
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 6);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 0);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.sdk->before_call = {};
  fixture.driver->deinit_hand();
  check_no_motion_state_calls(fixture);
}

void check_async_fault_during_apply_writes_rolls_back() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());

  fixture.sdk->fail_operation = "decode_canfd";
  bool delivered = false;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation != "set_sdo_drive_param") return;
    if (!delivered) {
      delivered = true;
      CanFdFrame frame;
      frame.id = 0x581;
      frame.len = 8;
      fixture.transport->deliver(frame);
    }
    deliver_latest_write_response(fixture, 0x60U);
  };

  const auto error = expect_exception<std::runtime_error>(
      [&] { (void)fixture.driver->apply_feedback_period_20ms(); });
  check_fault_message(error, "decode_canfd", 7, "async");
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 12);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 7);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.sdk->before_call = {};
  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
  CHECK(fixture.driver->init_for_provisioning());
  const auto restored = fixture.driver->show_feedback_period();
  CHECK_EQ(restored.before.front(), 100U);
  CHECK(std::all_of(std::next(restored.before.begin()), restored.before.end(),
                    [](unsigned int value) {
                      return value == kFeedbackPeriod20msUnits;
                    }));
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 18);
  fixture.driver->deinit_hand();
  check_no_motion_state_calls(fixture);
}

void check_async_fault_before_apply_save_rolls_back() {
  Fixture fixture(LHandProModel::Dof6S);
  fixture.sdk->set_sdo_value(kFeedbackPeriodIndexes.front(), 100U);
  CHECK(fixture.driver->init_for_provisioning());

  fixture.sdk->fail_operation = "decode_canfd";
  int reads = 0;
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "set_sdo_drive_param") {
      deliver_latest_write_response(fixture, 0x60U);
      return;
    }
    if (operation == "get_sdo_drive_param" && ++reads == 12) {
      CanFdFrame frame;
      frame.id = 0x581;
      frame.len = 8;
      fixture.transport->deliver(frame);
    }
  };

  const auto error = expect_exception<std::runtime_error>(
      [&] { (void)fixture.driver->apply_feedback_period_20ms(); });
  check_fault_message(error, "decode_canfd", 7, "async");
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 18);
  CHECK_EQ(fixture.sdk->count("set_sdo_drive_param"), 12);
  CHECK_EQ(fixture.sdk->count("save_sdo_drive_param"), 0);
  fixture.sdk->before_call = {};
  fixture.sdk->fail_operation.clear();
  fixture.driver->deinit_hand();
  CHECK(fixture.driver->init_for_provisioning());
  const auto restored = fixture.driver->show_feedback_period();
  CHECK_EQ(restored.before.front(), 100U);
  CHECK(std::all_of(std::next(restored.before.begin()), restored.before.end(),
                    [](unsigned int value) {
                      return value == kFeedbackPeriod20msUnits;
                    }));
  CHECK_EQ(fixture.sdk->count("get_sdo_drive_param"), 24);
  fixture.driver->deinit_hand();
  check_no_motion_state_calls(fixture);
}

}  // namespace

int main() {
  check_concurrent_dof_snapshot();
  check_constructor_and_home_wait_validation();
  check_home_wait_returns_on_fresh_completed_feedback();
  check_home_wait_rejects_position_outside_tolerance();
  check_home_wait_rejects_stale_zero_feedback();
  check_failure_rollback_and_retry();
  check_init_hand_rejects_conflicting_reinit();
  check_models_and_initializing_callbacks();
  check_runtime_feedback_uses_unit_multiplier_after_every_initial_ex();
  check_runtime_feedback_transmit_failure_aborts_initialization();
  check_ready_async_decode_faults();
  check_decode_exception_is_contained_as_async_fault();
  check_initializing_async_decode_fault_aborts_risky_init();
  check_async_fault_during_feedback_window_aborts_before_queries();
  check_completed_async_fault_blocks_later_risky_init();
  check_admitted_risky_init_serializes_async_fault();
  check_async_fault_precedes_final_ready_transition();
  check_rx_queued_during_no_home_decodes_after_release();
  check_registered_rx_blocks_final_ready_transition();
  check_final_commit_excludes_late_rx_registration();
  check_health_contract_and_state_guards();
  check_ready_sdk_failures_are_faults();
  check_faulted_call_policy_and_sticky_fault();
  check_fault_epoch_reset();
  check_failed_init_cleanup_preserves_primary();
  check_cleanup_threshold_and_late_init_matrix();
  check_cleanup_failure_matrix();
  check_faulted_deinit_cleanup_and_epoch_reset();
  check_destructor_contains_cleanup_failures();
  check_public_successes_and_cleanup_order();
  check_rx_drain();
  check_tx_drain_and_old_callback_silence();
  check_public_call_drain();
  check_early_stopping_rx_feedback();
  check_single_active_instance();
  check_slot_release_after_full_cleanup();
  check_provisioning_show_and_cleanup_without_motion();
  check_ready_sessions_reject_cross_purpose_initialization();
  check_provisioning_apply_and_fresh_session();
  check_provisioning_sdo_acks_are_consumed_and_may_arrive_early();
  check_provisioning_waits_for_each_exact_sdo_ack();
  check_provisioning_rejects_malformed_sdo_ack_lengths();
  check_provisioning_waits_for_ack_callback_completion();
  check_provisioning_sdo_abort_rolls_back_all_axes();
  check_provisioning_sdo_ack_timeout_rolls_back();
  check_provisioning_sync_write_failure_cancels_ack_wait();
  check_provisioning_save_requires_ack();
  check_provisioning_wrong_save_ack_times_out();
  check_provisioning_save_probe_does_not_complete_ack_wait();
  check_save_probe_outside_pending_save_remains_fail_closed();
  check_malformed_save_probes_remain_fail_closed();
  check_cleanup_cancels_pending_sdo_ack_wait();
  check_provisioning_state_purpose_and_model_guards();
  check_provisioning_initialization_failure_matrix();
  check_provisioning_show_surfaces_async_fault();
  check_provisioning_call_revalidates_replacement_session();
  check_async_fault_before_apply_save_rolls_back();
  check_async_fault_during_apply_writes_rolls_back();
  check_async_fault_before_apply_writes_cancels_transaction();
  check_provisioning_call_rejects_same_purpose_replacement();
  return 0;
}
