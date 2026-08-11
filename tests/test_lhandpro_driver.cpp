// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_driver.hpp"
#include "fakes/fake_canfd_transport.hpp"
#include "fakes/fake_lhandpro_sdk.hpp"
#include "test_support.hpp"

#include <linux/can.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
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

  explicit Fixture(LHandProModel model = LHandProModel::Dof16) {
    auto sdk_owner = std::make_unique<FakeLHandProSdk>();
    auto transport_owner = std::make_unique<FakeCanFdTransport>();
    sdk = sdk_owner.get();
    transport = transport_owner.get();
    if (model == LHandProModel::Dof16) {
      sdk->total_dof = 21;
      sdk->active_dof = 16;
    }
    driver = std::make_unique<LHandProDriver>(
        "can-test", model, 1, std::move(sdk_owner),
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

std::unique_ptr<LHandProDriver> make_driver(
    std::string interface, LHandProModel model, int node,
    std::unique_ptr<LHandProSdk> sdk = std::make_unique<FakeLHandProSdk>(),
    std::unique_ptr<CanFdTransport> transport =
        std::make_unique<FakeCanFdTransport>()) {
  return std::make_unique<LHandProDriver>(
      std::move(interface), model, node, std::move(sdk),
      std::move(transport));
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
    make_driver("", LHandProModel::Dof6, 1);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6, 0);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6, 128);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", static_cast<LHandProModel>(99), 1);
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6, 1, nullptr,
                std::make_unique<FakeCanFdTransport>());
  }));
  CHECK(throws_invalid_argument([] {
    make_driver("can-test", LHandProModel::Dof6, 1,
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
  }
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->deinit_hand();
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

void check_models_and_initializing_callbacks() {
  Fixture six(LHandProModel::Dof6);
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
  CHECK_EQ(six.sdk->hand_type, 0);
  CHECK_EQ(six.sdk->count("get_hand_type"), 2);
  CHECK_EQ(six.sdk->last_mode, 1);
  CHECK_EQ(six.sdk->last_node, 1);
  CHECK_EQ(six.sdk->last_decode_size, 5);
  CHECK_EQ(six.transport->open_interface, std::string("can-test"));
  CHECK_EQ(six.transport->open_ids,
           (std::vector<std::uint32_t>{0x501, 0x481, 0x581, 0x181}));

  const auto initial_frames = six.transport->sent_snapshot();
  CHECK_EQ(initial_frames.size(), 1U);
  CHECK_EQ(initial_frames.front().id, 0x601U);
  CHECK(!initial_frames.front().extended);
  CHECK_EQ(initial_frames.front().len, 9U);
  CHECK(initial_frames.front().brs);
  for (std::size_t index = 0; index < 9; ++index) {
    CHECK_EQ(initial_frames.front().data[index], index);
  }

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
  CHECK(six.driver->init_hand(true, true, 0.0F));
  CHECK_EQ(six.sdk->count("create"), create_calls);
  CHECK_EQ(six.transport->open_calls.load(), open_calls);

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

  Fixture wrong_initial_model(LHandProModel::Dof6);
  wrong_initial_model.sdk->reported_hand_type_override = 2;
  CHECK(!wrong_initial_model.driver->init_hand(false, false, 0.0F));
  CHECK(wrong_initial_model.released_once());
  CHECK_EQ(wrong_initial_model.transport->open_calls.load(), 0);

  Fixture wrong_second_model(LHandProModel::Dof6);
  int model_reads = 0;
  wrong_second_model.sdk->before_call = [&](const std::string& operation) {
    if (operation == "get_hand_type" && ++model_reads == 2) {
      wrong_second_model.sdk->reported_hand_type_override = 2;
    }
  };
  CHECK(!wrong_second_model.driver->init_hand(false, false, 0.0F));
  CHECK(wrong_second_model.released_once());
  CHECK_EQ(wrong_second_model.sdk->count("get_hand_type"), 2);

  check_dof_mismatch_and_retry(LHandProModel::Dof6, 12, 6, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6, 10, 6, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6, 11, 5, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6, 11, 7, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof6, 21, 16, 11, 6);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 22, 16, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 20, 16, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 21, 15, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 21, 17, 21, 16);
  check_dof_mismatch_and_retry(LHandProModel::Dof16, 11, 6, 21, 16);
}

void check_public_contracts_and_cleanup_order() {
  Fixture fixture;
  fixture.driver->move_motors(1);
  fixture.driver->stop_motors(1);
  fixture.driver->set_target_position(1, 100);
  fixture.driver->set_target_angle(1, 10.0F);
  fixture.driver->set_position_velocity(1, 20);
  fixture.driver->set_max_current(1, 30);
  fixture.driver->set_enable(1, true);
  fixture.driver->home_motors(1);
  fixture.driver->set_move_no_home(1);
  fixture.driver->clear_alarm(1);
  CHECK_EQ(fixture.sdk->count("move_motors"), 0);
  CHECK_EQ(fixture.sdk->count("stop_motors"), 0);
  CHECK_EQ(fixture.driver->get_now_position(1), 0);
  CHECK_EQ(fixture.driver->get_now_angle(1), 0.0F);
  CHECK_EQ(fixture.driver->get_now_status(1), 0);
  CHECK_EQ(fixture.driver->get_now_current(1), 0);
  CHECK_EQ(fixture.driver->get_now_alarm(1), 0);

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

  fixture.sdk->fail_operation = "get_now_position";
  CHECK_EQ(fixture.driver->get_now_position(1), 0);
  fixture.sdk->fail_operation = "get_now_angle";
  CHECK_EQ(fixture.driver->get_now_angle(1), 0.0F);
  fixture.sdk->fail_operation = "get_now_status";
  CHECK_EQ(fixture.driver->get_now_status(1), 0);
  fixture.sdk->fail_operation = "get_now_current";
  CHECK_EQ(fixture.driver->get_now_current(1), 0);
  fixture.sdk->fail_operation = "get_now_alarm";
  CHECK_EQ(fixture.driver->get_now_alarm(1), 0);
  fixture.sdk->fail_operation = "move_motors";
  fixture.driver->move_motors(1);
  CHECK_EQ(fixture.sdk->count("move_motors"), 2);
  fixture.sdk->fail_operation.clear();

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
  fixture.driver->stop_motors(1);
  CHECK_EQ(fixture.sdk->count("stop_motors"), stopped_calls);
  CHECK_EQ(fixture.driver->get_now_position(1), 0);
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
  fixture.driver->stop_motors(1);
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

}  // namespace

int main() {
  check_concurrent_dof_snapshot();
  check_constructor_and_home_wait_validation();
  check_failure_rollback_and_retry();
  check_models_and_initializing_callbacks();
  check_public_contracts_and_cleanup_order();
  check_rx_drain();
  check_tx_drain_and_old_callback_silence();
  check_public_call_drain();
  check_early_stopping_rx_feedback();
  check_single_active_instance();
  check_slot_release_after_full_cleanup();
  return 0;
}
