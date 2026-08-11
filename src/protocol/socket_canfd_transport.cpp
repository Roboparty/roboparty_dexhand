// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "protocol/socket_canfd_transport.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace roboparty::dexhand::detail {

int LinuxSocketOps::socket(int domain, int type, int protocol) noexcept {
  return ::socket(domain, type, protocol);
}
int LinuxSocketOps::set_option(int fd, int level, int name, const void* value,
                               socklen_t size) noexcept {
  return ::setsockopt(fd, level, name, value, size);
}
int LinuxSocketOps::interface_index(int fd,
                                    const std::string& name) noexcept {
  if (name.empty() || name.size() >= IFNAMSIZ) {
    errno = ENAMETOOLONG;
    return -1;
  }
  ifreq request{};
  std::memcpy(request.ifr_name, name.c_str(), name.size() + 1);
  if (::ioctl(fd, SIOCGIFINDEX, &request) < 0) return -1;
  return request.ifr_ifindex;
}
int LinuxSocketOps::bind(int fd, const sockaddr_can& address) noexcept {
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
}
int LinuxSocketOps::poll_readable(int fd, int timeout_ms) noexcept {
  pollfd descriptor{fd, POLLIN, 0};
  return ::poll(&descriptor, 1, timeout_ms);
}
ssize_t LinuxSocketOps::read(int fd, canfd_frame& frame) noexcept {
  return ::read(fd, &frame, CANFD_MTU);
}
ssize_t LinuxSocketOps::write(int fd, const canfd_frame& frame) noexcept {
  return ::write(fd, &frame, CANFD_MTU);
}
int LinuxSocketOps::close(int fd) noexcept { return ::close(fd); }
int LinuxSocketOps::last_error() const noexcept { return errno; }

SocketCanFdTransport::SocketCanFdTransport(std::shared_ptr<SocketOps> ops)
    : ops_(std::move(ops)) {
  if (!ops_) throw std::invalid_argument("SocketOps must not be null");
}
SocketCanFdTransport::~SocketCanFdTransport() { close(); }

bool SocketCanFdTransport::open(const std::string& interface,
                                const std::vector<std::uint32_t>& standard_ids) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  close_locked_();
  if (interface.empty() || standard_ids.empty() ||
      std::any_of(standard_ids.begin(), standard_ids.end(),
                  [](std::uint32_t id) { return id > CAN_SFF_MASK; })) {
    spdlog::error("Invalid CAN-FD interface or standard filter list");
    return false;
  }

  const int candidate = ops_->socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  if (candidate < 0) {
    spdlog::error("SocketCAN socket failed: errno={}", ops_->last_error());
    return false;
  }
  auto fail = [&](const char* operation) {
    spdlog::error("SocketCAN {} failed on {}: errno={}", operation, interface,
                  ops_->last_error());
    ops_->close(candidate);
    return false;
  };

  const int enabled = 1;
  if (ops_->set_option(candidate, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enabled,
                       sizeof(enabled)) < 0) {
    return fail("CAN_RAW_FD_FRAMES");
  }

  std::vector<can_filter> filters;
  filters.reserve(standard_ids.size());
  for (std::uint32_t id : standard_ids) {
    filters.push_back(can_filter{id, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG});
  }
  if (ops_->set_option(candidate, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                       static_cast<socklen_t>(filters.size() * sizeof(can_filter))) < 0) {
    return fail("CAN_RAW_FILTER");
  }

  const int index = ops_->interface_index(candidate, interface);
  if (index < 0) return fail("SIOCGIFINDEX");
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = index;
  if (ops_->bind(candidate, address) < 0) return fail("bind");

  {
    std::lock_guard<std::mutex> lock(transmit_mutex_);
    fd_ = candidate;
    running_.store(true, std::memory_order_release);
    try {
      receive_thread_ = std::thread(&SocketCanFdTransport::receive_loop_, this);
    } catch (const std::exception& error) {
      spdlog::error("SocketCAN receive thread failed: {}", error.what());
      running_.store(false, std::memory_order_release);
      fd_ = -1;
      ops_->close(candidate);
      return false;
    }
  }
  return true;
}

bool SocketCanFdTransport::transmit(const CanFdFrame& source) noexcept {
  if (source.len > source.data.size()) return false;
  if (source.extended) {
    if (source.id > CAN_EFF_MASK) return false;
  } else if (source.id > CAN_SFF_MASK) {
    return false;
  }

  canfd_frame frame{};
  frame.can_id = source.extended ? (source.id | CAN_EFF_FLAG) : source.id;
  frame.len = source.len;
  frame.flags = source.len > 8 ? CANFD_BRS : 0;
  std::copy_n(source.data.begin(), source.len, frame.data);

  std::lock_guard<std::mutex> lock(transmit_mutex_);
  if (!running_.load(std::memory_order_acquire) || fd_ < 0) return false;
  const auto written = ops_->write(fd_, frame);
  if (written != CANFD_MTU) {
    spdlog::error("SocketCAN write failed or short: result={}, errno={}",
                  written, ops_->last_error());
    return false;
  }
  return true;
}

void SocketCanFdTransport::set_receive_callback(ReceiveCallback callback) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (!running_.load(std::memory_order_acquire)) return;
  receive_gate_.close_and_wait();
  const bool enable = static_cast<bool>(callback);
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    receive_callback_ = std::move(callback);
  }
  if (enable) receive_gate_.open();
}

void SocketCanFdTransport::clear_receive_callback() noexcept {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  clear_receive_callback_locked_();
}

void SocketCanFdTransport::clear_receive_callback_locked_() noexcept {
  receive_gate_.close_and_wait();
  std::lock_guard<std::mutex> lock(callback_mutex_);
  receive_callback_ = {};
}

void SocketCanFdTransport::dispatch_(const canfd_frame& source) noexcept {
  auto lease = receive_gate_.try_enter();
  if (!lease) return;
  ReceiveCallback callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback = receive_callback_;
  }
  if (!callback) return;
  CanFdFrame frame;
  frame.extended = (source.can_id & CAN_EFF_FLAG) != 0;
  frame.id = source.can_id & (frame.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  frame.brs = (source.flags & CANFD_BRS) != 0;
  frame.len = static_cast<std::uint8_t>(
      std::min<std::size_t>(source.len, frame.data.size()));
  std::copy_n(source.data, frame.len, frame.data.begin());
  try { callback(frame); } catch (...) { }
}

void SocketCanFdTransport::receive_loop_() noexcept {
  while (running_.load(std::memory_order_acquire)) {
    const int ready = ops_->poll_readable(fd_, 50);
    if (!running_.load(std::memory_order_acquire)) break;
    if (ready == 0) continue;
    if (ready < 0) {
      if (ops_->last_error() != EINTR) {
        spdlog::error("SocketCAN poll failed: errno={}", ops_->last_error());
      }
      continue;
    }
    canfd_frame frame{};
    const auto bytes = ops_->read(fd_, frame);
    if (bytes == CANFD_MTU) {
      dispatch_(frame);
    } else if (bytes < 0 && ops_->last_error() != EAGAIN &&
               ops_->last_error() != EWOULDBLOCK) {
      spdlog::error("SocketCAN read failed: errno={}", ops_->last_error());
    }
  }
}

void SocketCanFdTransport::close_locked_() noexcept {
  {
    std::lock_guard<std::mutex> lock(transmit_mutex_);
    running_.store(false, std::memory_order_release);
  }
  clear_receive_callback_locked_();
  if (receive_thread_.joinable()) receive_thread_.join();
  std::lock_guard<std::mutex> lock(transmit_mutex_);
  if (fd_ >= 0) {
    ops_->close(fd_);
    fd_ = -1;
  }
}

void SocketCanFdTransport::close() noexcept {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  close_locked_();
}

}  // namespace roboparty::dexhand::detail
