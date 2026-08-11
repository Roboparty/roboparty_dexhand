// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "protocol/socket_canfd_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace roboparty::dexhand::detail;

namespace {

constexpr std::uint32_t kAllowedId = 0x321;
constexpr std::uint8_t kFirstMarker = 0xA1;
constexpr std::uint8_t kSecondMarker = 0xB2;
constexpr std::uint8_t kSentinelMarker = 0xD3;
constexpr std::uint8_t kFinalMarker = 0xE4;

enum class Receiver { First, Second };

struct ObservationSnapshot {
  int first_saw_first{0};
  int first_saw_second{0};
  int first_saw_sentinel{0};
  int first_saw_final{0};
  int second_saw_first{0};
  int second_saw_second{0};
  int second_saw_sentinel{0};
  int second_saw_final{0};
  int unexpected{0};
};

class ObservationState {
 public:
  void observe(Receiver receiver, const CanFdFrame& frame) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (frame.id != kAllowedId || frame.len == 0) {
        ++snapshot_.unexpected;
      } else if (frame.data[0] == kFirstMarker) {
        increment_(receiver, snapshot_.first_saw_first,
                   snapshot_.second_saw_first);
      } else if (frame.data[0] == kSecondMarker) {
        increment_(receiver, snapshot_.first_saw_second,
                   snapshot_.second_saw_second);
      } else if (frame.data[0] == kSentinelMarker) {
        increment_(receiver, snapshot_.first_saw_sentinel,
                   snapshot_.second_saw_sentinel);
      } else if (frame.data[0] == kFinalMarker) {
        increment_(receiver, snapshot_.first_saw_final,
                   snapshot_.second_saw_final);
      } else {
        ++snapshot_.unexpected;
      }
    }
    received_.notify_all();
  }

  bool wait_for_second_first(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return received_.wait_for(
        lock, timeout, [&] { return snapshot_.second_saw_first == 1; });
  }

  bool wait_for_second_sentinel(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return received_.wait_for(
        lock, timeout, [&] { return snapshot_.second_saw_sentinel == 1; });
  }

  bool wait_for_first_second(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return received_.wait_for(
        lock, timeout, [&] { return snapshot_.first_saw_second == 1; });
  }

  bool wait_for_second_final(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return received_.wait_for(
        lock, timeout, [&] { return snapshot_.second_saw_final == 1; });
  }

  ObservationSnapshot snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
  }

 private:
  static void increment_(Receiver receiver, int& first, int& second) {
    ++(receiver == Receiver::First ? first : second);
  }

  mutable std::mutex mutex_;
  std::condition_variable received_;
  ObservationSnapshot snapshot_;
};

bool first_own_messages_absent_after_barrier(
    const ObservationSnapshot& observed) {
  return observed.first_saw_second == 1 && observed.first_saw_first == 0 &&
         observed.first_saw_sentinel == 0 && observed.unexpected == 0;
}

bool second_own_message_absent_after_barrier(
    const ObservationSnapshot& observed) {
  return observed.second_saw_final == 1 && observed.second_saw_second == 0 &&
         observed.unexpected == 0;
}

CanFdFrame frame(std::uint32_t id, std::uint8_t marker) {
  CanFdFrame result;
  result.id = id;
  result.len = 8;
  result.data[0] = marker;
  return result;
}

int run_observation_state_self_test() {
  ObservationState observations;
  observations.observe(Receiver::Second, frame(0x322, 0xCC));
  observations.observe(Receiver::Second,
                       frame(kAllowedId, kSentinelMarker));
  if (!observations.wait_for_second_sentinel(std::chrono::milliseconds(0))) {
    return 1;
  }
  auto observed = observations.snapshot();
  if (observed.second_saw_sentinel != 1 || observed.unexpected != 1) {
    return 1;
  }

  ObservationState clean_first_barrier;
  clean_first_barrier.observe(Receiver::First,
                              frame(kAllowedId, kSecondMarker));
  if (!clean_first_barrier.wait_for_first_second(
          std::chrono::milliseconds(0)) ||
      !first_own_messages_absent_after_barrier(
          clean_first_barrier.snapshot())) {
    return 1;
  }
  ObservationState delayed_first_own;
  delayed_first_own.observe(Receiver::First, frame(kAllowedId, kFirstMarker));
  delayed_first_own.observe(Receiver::First,
                            frame(kAllowedId, kSentinelMarker));
  delayed_first_own.observe(Receiver::First,
                            frame(kAllowedId, kSecondMarker));
  if (!delayed_first_own.wait_for_first_second(
          std::chrono::milliseconds(0)) ||
      first_own_messages_absent_after_barrier(delayed_first_own.snapshot())) {
    return 1;
  }

  ObservationState clean_second_barrier;
  clean_second_barrier.observe(Receiver::Second,
                               frame(kAllowedId, kFinalMarker));
  if (!clean_second_barrier.wait_for_second_final(
          std::chrono::milliseconds(0)) ||
      !second_own_message_absent_after_barrier(
          clean_second_barrier.snapshot())) {
    return 1;
  }
  ObservationState delayed_second_own;
  delayed_second_own.observe(Receiver::Second,
                             frame(kAllowedId, kSecondMarker));
  delayed_second_own.observe(Receiver::Second,
                             frame(kAllowedId, kFinalMarker));
  if (!delayed_second_own.wait_for_second_final(
          std::chrono::milliseconds(0)) ||
      second_own_message_absent_after_barrier(delayed_second_own.snapshot())) {
    return 1;
  }

  ObservationState synchronized;
  std::thread notifier([&] {
    synchronized.observe(Receiver::Second,
                         frame(kAllowedId, kFirstMarker));
  });
  const bool received =
      synchronized.wait_for_second_first(std::chrono::seconds(1));
  notifier.join();
  observed = synchronized.snapshot();
  return received && observed.second_saw_first == 1 &&
                 observed.unexpected == 0
             ? 0
             : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--self-test") {
    return run_observation_state_self_test();
  }
  if (argc != 2) {
    std::cerr << "usage: vcan_two_socket <interface>\n";
    return 2;
  }

  const std::string interface = argv[1];
  ObservationState observations;
  SocketCanFdTransport first;
  SocketCanFdTransport second;
  if (!first.open(interface, {kAllowedId}) ||
      !second.open(interface, {kAllowedId})) {
    std::cerr << "failed to open both SocketCAN transports\n";
    return 1;
  }
  first.set_receive_callback([&](const CanFdFrame& received) {
    observations.observe(Receiver::First, received);
  });
  second.set_receive_callback([&](const CanFdFrame& received) {
    observations.observe(Receiver::Second, received);
  });

  if (!first.transmit(frame(kAllowedId, kFirstMarker)) ||
      !observations.wait_for_second_first(std::chrono::seconds(1))) {
    return 1;
  }
  auto observed = observations.snapshot();
  if (observed.second_saw_first != 1 || observed.unexpected != 0) {
    return 1;
  }

  if (!first.transmit(frame(0x322, 0xCC)) ||
      !first.transmit(frame(kAllowedId, kSentinelMarker)) ||
      !observations.wait_for_second_sentinel(std::chrono::seconds(1))) {
    return 1;
  }
  observed = observations.snapshot();
  if (observed.second_saw_sentinel != 1 || observed.unexpected != 0) {
    return 1;
  }

  if (!second.transmit(frame(kAllowedId, kSecondMarker)) ||
      !observations.wait_for_first_second(std::chrono::seconds(1))) {
    return 1;
  }
  observed = observations.snapshot();
  if (!first_own_messages_absent_after_barrier(observed) ||
      observed.second_saw_first != 1 || observed.second_saw_sentinel != 1) {
    return 1;
  }

  if (!first.transmit(frame(kAllowedId, kFinalMarker)) ||
      !observations.wait_for_second_final(std::chrono::seconds(1))) {
    return 1;
  }
  observed = observations.snapshot();
  if (!first_own_messages_absent_after_barrier(observed) ||
      !second_own_message_absent_after_barrier(observed) ||
      observed.second_saw_first != 1 || observed.second_saw_sentinel != 1) {
    return 1;
  }
  first.close();
  second.close();
  return 0;
}
