// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "fakes/fake_socket_ops.hpp"
#include "logging_test_support.hpp"
#include "protocol/socket_canfd_transport.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace roboparty::dexhand::detail;

void check_open_failure(const std::function<void(FakeSocketOps&)>& configure,
                        const std::string& expected_operation) {
  ScopedTestLoggers loggers;
  auto ops = std::make_shared<FakeSocketOps>();
  ops->error_number = ENETDOWN;
  ops->overwrite_error_on_close = true;
  configure(*ops);
  SocketCanFdTransport transport(ops);
  CHECK(!transport.open("can-test", {0x501}));
  CHECK_EQ(ops->close_calls, 1);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 1);
  const auto message = loggers.named_sink()->messages();
  CHECK(message.find(expected_operation) != std::string::npos);
  CHECK(message.find("can-test") != std::string::npos);
  CHECK(message.find("errno=" + std::to_string(ENETDOWN)) !=
        std::string::npos);
  CHECK(message.find(std::system_category().message(ENETDOWN)) !=
        std::string::npos);
  CHECK(message.find(std::system_category().message(ESTALE)) ==
        std::string::npos);
}

void check_callback_shutdown_serialization() {
  auto ops = std::make_shared<FakeSocketOps>();
  ops->block_poll();
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501}));
  CHECK(ops->wait_for_poll_blocked(std::chrono::seconds(2)));

  CanFdFrame frame;
  frame.id = 0x601;
  CHECK(transport.transmit(frame));
  auto closing = std::async(std::launch::async, [&transport] {
    transport.close();
  });
  const auto stop_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  bool stopped = false;
  while (std::chrono::steady_clock::now() < stop_deadline) {
    if (!transport.transmit(frame)) {
      stopped = true;
      break;
    }
    std::this_thread::yield();
  }
  CHECK(stopped);
  CHECK(closing.wait_for(std::chrono::milliseconds(0)) ==
        std::future_status::timeout);

  std::promise<void> replacement_started;
  auto replacement_started_future = replacement_started.get_future();
  auto callback_marker = std::make_shared<int>(0);
  std::weak_ptr<int> weak_callback_marker = callback_marker;
  auto replacement = std::async(
      std::launch::async,
      [&transport, &replacement_started,
       marker = std::move(callback_marker)]() mutable {
        replacement_started.set_value();
        transport.set_receive_callback(
            [marker = std::move(marker)](const CanFdFrame&) {});
      });
  CHECK(replacement_started_future.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  CHECK(replacement.wait_for(std::chrono::milliseconds(100)) ==
        std::future_status::timeout);

  ops->release_poll();
  CHECK(closing.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  closing.get();
  CHECK(replacement.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  replacement.get();
  CHECK(weak_callback_marker.expired());
  CHECK(!transport.transmit(frame));
  CHECK_EQ(ops->close_calls, 1);
}

bool wait_for_log(const ScopedTestLoggers& loggers) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (loggers.default_sink()->count() > 0 ||
        loggers.named_sink()->count() > 0) {
      return true;
    }
    std::this_thread::yield();
  }
  return false;
}

void check_open_error_details() {
  ScopedTestLoggers loggers;
  auto ops = std::make_shared<FakeSocketOps>();
  ops->socket_result = -1;
  ops->error_number = ENODEV;
  SocketCanFdTransport transport(ops);
  CHECK(!transport.open("can-missing", {0x501}));
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 1);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);
  const auto message = loggers.named_sink()->messages();
  CHECK(message.find("socket") != std::string::npos);
  CHECK(message.find("can-missing") != std::string::npos);
  CHECK(message.find("errno=" + std::to_string(ENODEV)) !=
        std::string::npos);
  CHECK(message.find(std::system_category().message(ENODEV)) !=
        std::string::npos);
}

void check_named_logging_and_errno_capture() {
  ScopedTestLoggers loggers;
  auto ops = std::make_shared<FakeSocketOps>();
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501}));

  CanFdFrame frame;
  frame.id = 0x601;
  ops->write_result = -1;
  ops->error_number = EACCES;
  loggers.named_sink()->clear();
  loggers.drop_named();
  CHECK(!transport.transmit(frame));
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 0);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);

  loggers.register_named();
  loggers.named_sink()->clear();
  CHECK(!transport.transmit(frame));
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 1);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 2);
  const auto failure_text = loggers.named_sink()->messages();
  CHECK(failure_text.find("errno=" + std::to_string(EACCES)) !=
        std::string::npos);
  CHECK(failure_text.find(std::system_category().message(EACCES)) !=
        std::string::npos);

  loggers.named_sink()->clear();
  const int error_calls_before_short =
      ops->last_error_calls.load(std::memory_order_relaxed);
  ops->write_result = CANFD_MTU - 1;
  ops->error_number = ESTALE;
  CHECK(!transport.transmit(frame));
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed),
           error_calls_before_short);
  const auto short_text = loggers.named_sink()->messages();
  CHECK(short_text.find("short") != std::string::npos);
  CHECK(short_text.find("errno=") == std::string::npos);
  CHECK(short_text.find(std::system_category().message(ESTALE)) ==
        std::string::npos);

  loggers.named_sink()->clear();
  loggers.named_sink()->set_throw_on_log(true);
  loggers.named_logger()->set_error_handler(
      [](const std::string&) { throw 7; });
  ops->write_result = -1;
  ops->error_number = EIO;
  CHECK(!transport.transmit(frame));
  CHECK_EQ(loggers.named_sink()->count(), 1);
  CHECK_EQ(loggers.default_sink()->count(), 0);
  transport.close();
}

void check_concurrent_named_logging() {
  ScopedTestLoggers loggers;
  constexpr int kThreads = 8;
  std::atomic<int> ready{0};
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  std::vector<std::shared_ptr<FakeSocketOps>> operations;
  std::vector<std::thread> openers;
  operations.reserve(kThreads);
  openers.reserve(kThreads);
  for (int index = 0; index < kThreads; ++index) {
    auto ops = std::make_shared<FakeSocketOps>();
    ops->socket_result = -1;
    ops->error_number = EIO;
    operations.push_back(ops);
    openers.emplace_back([&, ops] {
      ready.fetch_add(1, std::memory_order_release);
      release.wait();
      SocketCanFdTransport transport(ops);
      CHECK(!transport.open("can-test", {0x501}));
    });
  }
  while (ready.load(std::memory_order_acquire) != kThreads) {
    std::this_thread::yield();
  }
  release_promise.set_value();
  for (auto& opener : openers) opener.join();

  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), kThreads);
  for (const auto& ops : operations) {
    CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);
  }
}

void check_receive_worker_logging_is_noexcept() {
  ScopedTestLoggers loggers;
  loggers.named_sink()->set_throw_on_log(true);
  loggers.named_logger()->set_error_handler(
      [](const std::string&) { throw 11; });
  auto ops = std::make_shared<FakeSocketOps>();
  ops->error_number = EIO;
  ops->queue_poll_result(-1);
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501}));
  CHECK(wait_for_log(loggers));
  transport.close();
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 1);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);
  const auto message = loggers.named_sink()->messages();
  CHECK(message.find("errno=" + std::to_string(EIO)) != std::string::npos);
  CHECK(message.find(std::system_category().message(EIO)) !=
        std::string::npos);
}

void check_receive_worker_read_error_details() {
  ScopedTestLoggers loggers;
  auto ops = std::make_shared<FakeSocketOps>();
  ops->error_number = EBADMSG;
  ops->queue_poll_result(1);
  ops->queue_read_result(-1);
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501}));
  CHECK(wait_for_log(loggers));
  transport.close();
  CHECK_EQ(loggers.default_sink()->count(), 0);
  CHECK_EQ(loggers.named_sink()->count(), 1);
  CHECK_EQ(ops->last_error_calls.load(std::memory_order_relaxed), 1);
  const auto message = loggers.named_sink()->messages();
  CHECK(message.find("read") != std::string::npos);
  CHECK(message.find("errno=" + std::to_string(EBADMSG)) !=
        std::string::npos);
  CHECK(message.find(std::system_category().message(EBADMSG)) !=
        std::string::npos);
}

int main() {
  auto ops = std::make_shared<FakeSocketOps>();
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501, 0x481, 0x581, 0x181}));
  CHECK_EQ(ops->filters.size(), 4U);
  CHECK_EQ(ops->recv_own_msgs_calls, 0);
  const std::vector<std::uint32_t> expected_ids{0x501, 0x481, 0x581, 0x181};
  for (std::size_t index = 0; index < expected_ids.size(); ++index) {
    CHECK_EQ(ops->filters[index].can_id, expected_ids[index]);
    CHECK_EQ(ops->filters[index].can_mask,
             CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG);
  }

  CanFdFrame frame;
  frame.id = 0x601;
  frame.len = 8;
  frame.brs = true;
  CHECK(transport.transmit(frame));
  CHECK_EQ(ops->last_written.len, 8);
  CHECK_EQ(ops->last_written.flags & CANFD_BRS, 0);

  frame.len = 9;
  frame.brs = false;
  CHECK(transport.transmit(frame));
  CHECK((ops->last_written.flags & CANFD_BRS) != 0);

  frame.id = 0x1ABCDE;
  frame.extended = true;
  CHECK(transport.transmit(frame));
  CHECK_EQ(ops->last_written.can_id, 0x1ABCDEU | CAN_EFF_FLAG);
  frame.id = CAN_EFF_MASK + 1U;
  CHECK(!transport.transmit(frame));
  frame.extended = false;
  frame.id = CAN_SFF_MASK + 1U;
  CHECK(!transport.transmit(frame));

  ops->write_result = CANFD_MTU - 1;
  frame.id = 0x601;
  CHECK(!transport.transmit(frame));
  frame.len = 65;
  CHECK(!transport.transmit(frame));

  std::promise<void> callback_entered;
  std::promise<void> release_callback;
  auto release = release_callback.get_future().share();
  std::atomic<int> callback_count{0};
  transport.set_receive_callback([&](const CanFdFrame& received) {
    CHECK_EQ(received.id, 0x501U);
    CHECK_EQ(received.len, 3U);
    ++callback_count;
    callback_entered.set_value();
    release.wait();
  });
  canfd_frame incoming{};
  incoming.can_id = 0x501;
  incoming.len = 3;
  ops->queue(incoming);
  CHECK(callback_entered.get_future().wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  auto clearing = std::async(std::launch::async, [&transport] {
    transport.clear_receive_callback();
  });
  CHECK(clearing.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  release_callback.set_value();
  CHECK(clearing.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  ops->queue(incoming);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_EQ(callback_count.load(), 1);

  transport.close();
  transport.close();
  CHECK(!transport.transmit(CanFdFrame{}));
  CHECK_EQ(ops->close_calls, 1);

  check_callback_shutdown_serialization();
  check_open_error_details();
  check_named_logging_and_errno_capture();
  check_concurrent_named_logging();
  check_receive_worker_logging_is_noexcept();
  check_receive_worker_read_error_details();

  check_open_failure(
      [](FakeSocketOps& fake) { fake.fd_frames_result = -1; },
      "CAN_RAW_FD_FRAMES");
  check_open_failure([](FakeSocketOps& fake) { fake.filters_result = -1; },
                     "CAN_RAW_FILTER");
  check_open_failure(
      [](FakeSocketOps& fake) { fake.interface_index_result = -1; },
      "SIOCGIFINDEX");
  check_open_failure([](FakeSocketOps& fake) { fake.bind_result = -1; },
                     "bind");
  return 0;
}
