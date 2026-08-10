#include "fakes/fake_socket_ops.hpp"
#include "protocol/socket_canfd_transport.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

using namespace roboparty::dexhand::detail;

void check_open_failure(const std::function<void(FakeSocketOps&)>& configure,
                        int expected_closes) {
  auto ops = std::make_shared<FakeSocketOps>();
  configure(*ops);
  SocketCanFdTransport transport(ops);
  CHECK(!transport.open("can-test", {0x501}));
  CHECK_EQ(ops->close_calls, expected_closes);
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

  check_open_failure([](FakeSocketOps& fake) { fake.socket_result = -1; }, 0);
  check_open_failure([](FakeSocketOps& fake) { fake.fd_frames_result = -1; }, 1);
  check_open_failure([](FakeSocketOps& fake) { fake.filters_result = -1; }, 1);
  check_open_failure(
      [](FakeSocketOps& fake) { fake.interface_index_result = -1; }, 1);
  check_open_failure([](FakeSocketOps& fake) { fake.bind_result = -1; }, 1);
  return 0;
}
