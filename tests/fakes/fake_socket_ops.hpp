// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "protocol/socket_canfd_transport.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeSocketOps final : public SocketOps {
 public:
  int socket_result{42};
  int fd_frames_result{0};
  int filters_result{0};
  int interface_index_result{7};
  int bind_result{0};
  int poll_result{0};
  ssize_t write_result{CANFD_MTU};
  int error_number{EAGAIN};
  int close_error_number{ESTALE};
  bool overwrite_error_on_close{false};
  mutable std::atomic<int> last_error_calls{0};
  int close_calls{0};
  int recv_own_msgs_calls{0};
  int fd_frames_calls{0};
  std::string interface_name;
  std::vector<can_filter> filters;
  canfd_frame last_written{};

  int socket(int, int, int) noexcept override { return socket_result; }
  int set_option(int, int, int name, const void* value,
                 socklen_t size) noexcept override {
    if (name == CAN_RAW_RECV_OWN_MSGS) ++recv_own_msgs_calls;
    if (name == CAN_RAW_FD_FRAMES) {
      ++fd_frames_calls;
      return fd_frames_result;
    }
    if (name == CAN_RAW_FILTER) {
      const auto* first = static_cast<const can_filter*>(value);
      filters.assign(first, first + size / sizeof(can_filter));
      return filters_result;
    }
    return 0;
  }
  int interface_index(int, const std::string& name) noexcept override {
    interface_name = name;
    return interface_index_result;
  }
  int bind(int, const sockaddr_can&) noexcept override { return bind_result; }
  int poll_readable(int, int timeout_ms) noexcept override {
    {
      std::unique_lock<std::mutex> lock(poll_mutex_);
      if (block_poll_) {
        poll_blocked_ = true;
        poll_condition_.notify_all();
        poll_condition_.wait(lock, [this] { return !block_poll_; });
      }
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (!poll_results_.empty()) {
        const int result = poll_results_.front();
        poll_results_.pop_front();
        return result;
      }
      if (!incoming_.empty()) return 1;
    }
    if (poll_result != 0) return poll_result;
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return 0;
  }
  ssize_t read(int, canfd_frame& frame) noexcept override {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!read_results_.empty()) {
      const auto result = read_results_.front();
      read_results_.pop_front();
      return result;
    }
    if (incoming_.empty()) {
      error_number = EAGAIN;
      return -1;
    }
    frame = incoming_.front();
    incoming_.pop_front();
    return CANFD_MTU;
  }
  ssize_t write(int, const canfd_frame& frame) noexcept override {
    last_written = frame;
    return write_result;
  }
  int close(int) noexcept override {
    ++close_calls;
    if (overwrite_error_on_close) error_number = close_error_number;
    return 0;
  }
  int last_error() const noexcept override {
    last_error_calls.fetch_add(1, std::memory_order_relaxed);
    return error_number;
  }
  void queue_poll_result(int result) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    poll_results_.push_back(result);
  }
  void queue_read_result(ssize_t result) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    read_results_.push_back(result);
  }
  void queue(canfd_frame frame) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    incoming_.push_back(frame);
  }
  void block_poll() {
    std::lock_guard<std::mutex> lock(poll_mutex_);
    block_poll_ = true;
    poll_blocked_ = false;
  }
  bool wait_for_poll_blocked(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(poll_mutex_);
    return poll_condition_.wait_for(
        lock, timeout, [this] { return poll_blocked_; });
  }
  void release_poll() {
    {
      std::lock_guard<std::mutex> lock(poll_mutex_);
      block_poll_ = false;
    }
    poll_condition_.notify_all();
  }

 private:
  std::mutex poll_mutex_;
  std::condition_variable poll_condition_;
  bool block_poll_{false};
  bool poll_blocked_{false};
  std::mutex queue_mutex_;
  std::deque<int> poll_results_;
  std::deque<ssize_t> read_results_;
  std::deque<canfd_frame> incoming_;
};

}  // namespace roboparty::dexhand::detail
