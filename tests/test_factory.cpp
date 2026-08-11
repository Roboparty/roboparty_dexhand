// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "hand_driver.hpp"
#include "test_support.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class StartBarrier {
 public:
  explicit StartBarrier(int participants) : remaining_(participants) {}

  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    --remaining_;
    if (remaining_ == 0) {
      released_ = true;
      ready_.notify_all();
      return;
    }
    ready_.wait(lock, [this] { return released_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  int remaining_;
  bool released_{false};
};

void check_internal_logger_is_thread_safe() {
  spdlog::drop("dexhand");
  auto driver = HandDriver::create_hand("LHandPro", "canfd", "can0");
  auto registered = spdlog::get("dexhand");
  CHECK(driver != nullptr);
  CHECK(registered != nullptr);
  CHECK_EQ(registered->sinks().size(), 1U);
  CHECK(std::dynamic_pointer_cast<spdlog::sinks::stderr_color_sink_mt>(
            registered->sinks().front()) != nullptr);
  spdlog::drop("dexhand");
}

void check_concurrent_first_factory_creation() {
  constexpr int kRounds = 32;
  constexpr int kThreads = 16;

  for (int round = 0; round < kRounds; ++round) {
    spdlog::drop("dexhand");
    StartBarrier barrier(kThreads);
    std::atomic<int> exceptions{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int thread = 0; thread < kThreads; ++thread) {
      threads.emplace_back([&] {
        barrier.arrive_and_wait();
        try {
          if (!HandDriver::create_hand("LHandPro", "canfd", "can0")) {
            exceptions.fetch_add(1, std::memory_order_relaxed);
          }
        } catch (...) {
          exceptions.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    for (auto& thread : threads) thread.join();
    CHECK_EQ(exceptions.load(std::memory_order_relaxed), 0);
  }
}

void check_external_logger_reuse() {
  spdlog::drop("dexhand");
  auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  auto external = std::make_shared<spdlog::logger>("dexhand", sink);
  spdlog::register_logger(external);

  CHECK(HandDriver::create_hand("LHandPro", "canfd", "can0") != nullptr);
  CHECK(spdlog::get("dexhand") == external);
  spdlog::drop("dexhand");
}

void check_drop_recreates_registered_logger() {
  spdlog::drop("dexhand");
  auto first_driver = HandDriver::create_hand("LHandPro", "canfd", "can0");
  auto first_logger = spdlog::get("dexhand");
  CHECK(first_driver != nullptr);
  CHECK(first_logger != nullptr);

  spdlog::drop("dexhand");
  auto second_driver = HandDriver::create_hand("LHandPro", "canfd", "can0");
  auto second_logger = spdlog::get("dexhand");
  CHECK(second_driver != nullptr);
  CHECK(second_logger != nullptr);
  CHECK(second_logger != first_logger);
  spdlog::drop("dexhand");
}

void check_invalid(const std::string& hand_type,
                   const std::string& interface_type,
                   const std::string& interface, int model, int node,
                   const std::string& expected_text) {
  try {
    (void)HandDriver::create_hand(hand_type, interface_type, interface, model,
                                  node);
    CHECK(false);
  } catch (const std::invalid_argument& error) {
    CHECK(std::string(error.what()).find(expected_text) != std::string::npos);
  }
}

int main() {
  using Factory = std::shared_ptr<HandDriver> (*)(
      const std::string&, const std::string&, const std::string&, int, int);
  Factory factory = &HandDriver::create_hand;
  CHECK(factory != nullptr);

  check_internal_logger_is_thread_safe();
  check_concurrent_first_factory_creation();
  check_external_logger_reuse();
  check_drop_recreates_registered_logger();
  CHECK(HandDriver::create_hand("LHandPro", "canfd", "can0") != nullptr);
  check_invalid("Unknown", "canfd", "can0", 0, 1, "Unknown");
  check_invalid("LHandPro", "ethercanfd", "can0", 0, 1, "ethercanfd");
  check_invalid("LHandPro", "canfd", "", 0, 1, "empty");
  check_invalid("LHandPro", "canfd", "can0", -1, 1, "-1");
  check_invalid("LHandPro", "canfd", "can0", 2, 1, "2");
  check_invalid("LHandPro", "canfd", "can0", 0, 0, "0");
  check_invalid("LHandPro", "canfd", "can0", 0, 128, "128");
  spdlog::drop("dexhand");
  return 0;
}
