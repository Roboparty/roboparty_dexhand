// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "protocol/callback_gate.hpp"
#include "protocol/canfd_transport.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeCanFdTransport final : public CanFdTransport {
 public:
  bool open_result{true};
  bool transmit_result{true};
  std::string open_interface;
  std::vector<std::uint32_t> open_ids;
  std::vector<CanFdFrame> sent_frames;
  std::atomic<int> open_calls{0};
  std::atomic<int> clear_calls{0};
  std::atomic<int> close_calls{0};
  std::function<void()> before_transmit;
  std::function<void()> before_close;

  bool open(const std::string& interface,
            const std::vector<std::uint32_t>& ids) override {
    ++open_calls;
    open_interface = interface;
    open_ids = ids;
    is_open_.store(open_result, std::memory_order_release);
    return open_result;
  }

  bool transmit(const CanFdFrame& frame) noexcept override {
    if (!is_open_.load(std::memory_order_acquire)) return false;
    try {
      if (before_transmit) before_transmit();
      std::lock_guard<std::mutex> lock(mutex_);
      sent_frames.push_back(frame);
      return transmit_result;
    } catch (...) {
      return false;
    }
  }

  void set_receive_callback(ReceiveCallback callback) override {
    gate_.close_and_wait();
    const bool enable = static_cast<bool>(callback);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_active_ = enable;
      callback_ = std::move(callback);
    }
    if (enable) gate_.open();
  }

  void clear_receive_callback() noexcept override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!callback_active_) return;
      callback_active_ = false;
    }
    ++clear_calls;
    gate_.close_and_wait();
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = {};
  }

  void close() noexcept override {
    if (!is_open_.exchange(false, std::memory_order_acq_rel)) return;
    clear_receive_callback();
    try {
      if (before_close) before_close();
    } catch (...) {
    }
    ++close_calls;
  }

  void deliver(const CanFdFrame& frame) noexcept {
    auto lease = gate_.try_enter();
    if (!lease) return;
    ReceiveCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = callback_;
    }
    try {
      if (callback) callback(frame);
    } catch (...) {
    }
  }

  bool is_open() const noexcept {
    return is_open_.load(std::memory_order_acquire);
  }

  bool callback_active() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return callback_active_;
  }

  std::vector<CanFdFrame> sent_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sent_frames;
  }

 private:
  std::atomic<bool> is_open_{false};
  bool callback_active_{false};
  mutable std::mutex mutex_;
  ReceiveCallback callback_;
  CallbackGate gate_;
};

}  // namespace roboparty::dexhand::detail
