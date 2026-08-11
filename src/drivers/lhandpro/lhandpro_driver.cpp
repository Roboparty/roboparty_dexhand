// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_driver.hpp"

#include "protocol/callback_gate.hpp"
#include "protocol/socket_canfd_transport.hpp"

#include <linux/can.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace roboparty::dexhand::detail {

struct TxContext {
  CallbackGate gate;
  CanFdTransport* transport{nullptr};
};

struct SlotToken {};

}  // namespace roboparty::dexhand::detail

namespace {

using roboparty::dexhand::detail::CanFdFrame;
using roboparty::dexhand::detail::CanFdTransport;
using roboparty::dexhand::detail::CapiLHandProSdk;
using roboparty::dexhand::detail::DriverState;
using roboparty::dexhand::detail::LHandProModel;
using roboparty::dexhand::detail::SlotToken;
using roboparty::dexhand::detail::SocketCanFdTransport;
using roboparty::dexhand::detail::TxContext;

constexpr int kSdkSuccess = 0;
constexpr int kCanFdMode = 1;

std::mutex process_callback_mutex;
std::weak_ptr<TxContext> active_tx_context;
std::weak_ptr<SlotToken> active_slot_owner;

void log_boundary_error(const char* message) noexcept {
  try {
    spdlog::error("{}", message);
  } catch (...) {
  }
}

bool claim_process_slot(const std::shared_ptr<SlotToken>& owner) noexcept {
  if (!owner) return false;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (!active_slot_owner.expired()) return false;
    active_slot_owner = owner;
    return true;
  } catch (...) {
    log_boundary_error("LHandPro process slot claim failed");
    return false;
  }
}

void release_process_slot(const std::shared_ptr<SlotToken>& owner) noexcept {
  if (!owner) return;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (active_slot_owner.lock() == owner) active_slot_owner.reset();
  } catch (...) {
    log_boundary_error("LHandPro process slot release failed");
  }
}

bool publish_context(const std::shared_ptr<TxContext>& context) noexcept {
  if (!context || !context->transport) return false;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (!active_tx_context.expired()) return false;
    context->gate.open();
    active_tx_context = context;
    return true;
  } catch (...) {
    log_boundary_error("LHandPro transmit context publication failed");
    return false;
  }
}

void unpublish_context(const std::shared_ptr<TxContext>& context) noexcept {
  if (!context) return;
  try {
    {
      std::lock_guard<std::mutex> lock(process_callback_mutex);
      if (active_tx_context.lock() == context) active_tx_context.reset();
    }
    context->gate.close_and_wait();
  } catch (...) {
    log_boundary_error("LHandPro transmit context shutdown failed");
  }
}

bool transmit_bridge(unsigned int id, const unsigned char* data,
                     unsigned int size, int is_extended) noexcept {
  try {
    std::shared_ptr<TxContext> context;
    {
      std::lock_guard<std::mutex> lock(process_callback_mutex);
      context = active_tx_context.lock();
    }
    if (!context || !context->transport || (!data && size > 0) || size > 64) {
      return false;
    }

    auto lease = context->gate.try_enter();
    if (!lease) return false;

    CanFdFrame frame;
    frame.extended = is_extended != 0;
    if ((frame.extended && id > CAN_EFF_MASK) ||
        (!frame.extended && id > CAN_SFF_MASK)) {
      return false;
    }
    frame.id = id;
    frame.len = static_cast<std::uint8_t>(size);
    frame.brs = size > 8;
    if (size > 0) std::copy_n(data, size, frame.data.begin());
    return context->transport->transmit(frame);
  } catch (const std::exception& error) {
    try {
      spdlog::error("LHandPro transmit callback exception: {}", error.what());
    } catch (...) {
    }
  } catch (...) {
    log_boundary_error("LHandPro transmit callback exception");
  }
  return false;
}

}  // namespace

LHandProDriver::LHandProDriver(std::string can_interface,
                               LHandProModel model, int canfd_node_id)
    : LHandProDriver(std::move(can_interface), model, canfd_node_id,
                     std::make_unique<CapiLHandProSdk>(),
                     std::make_unique<SocketCanFdTransport>()) {}

LHandProDriver::LHandProDriver(
    std::string can_interface, LHandProModel model, int canfd_node_id,
    std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk,
    std::unique_ptr<CanFdTransport> transport)
    : model_(model),
      sdk_(std::move(sdk)),
      transport_(std::move(transport)),
      state_(DriverState::Created) {
  if (can_interface.empty()) {
    throw std::invalid_argument("CAN interface must not be empty");
  }
  if (canfd_node_id < 1 || canfd_node_id > 127) {
    throw std::invalid_argument("CAN-FD node ID must be in [1, 127]");
  }
  if (model != LHandProModel::Dof6 && model != LHandProModel::Dof16) {
    throw std::invalid_argument("Unsupported LHandPro model");
  }
  if (!sdk_ || !transport_) {
    throw std::invalid_argument("SDK and transport must not be null");
  }

  can_interface_ = std::move(can_interface);
  canfd_node_id_ = canfd_node_id;
  comm_type_ = HandCommType::CANFD;
}

LHandProDriver::~LHandProDriver() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  cleanup_locked_();
}

bool LHandProDriver::ready_() const noexcept {
  return state_.load(std::memory_order_acquire) == DriverState::Ready;
}

bool LHandProDriver::sdk_ok_(int code, const char* operation) const noexcept {
  if (code == kSdkSuccess) return true;
  try {
    logger_->error("LHandPro {} failed: SDK error={}", operation, code);
  } catch (...) {
  }
  return false;
}

int LHandProDriver::expected_vendor_model_() const noexcept {
  return model_ == LHandProModel::Dof6 ? 0 : 2;
}

int LHandProDriver::expected_total_dof_() const noexcept {
  return model_ == LHandProModel::Dof6 ? 6 : 16;
}

bool LHandProDriver::init_hand(bool enable_motors, bool home_motors,
                               float home_wait_time) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  const auto current = state_.load(std::memory_order_acquire);
  if (current == DriverState::Ready) return true;
  if (current != DriverState::Created) return false;
  if (!std::isfinite(home_wait_time) || home_wait_time < 0.0F) {
    try {
      logger_->error("home_wait_time must be finite and nonnegative");
    } catch (...) {
    }
    return false;
  }

  state_.store(DriverState::Initializing, std::memory_order_release);
  auto fail = [this]() noexcept {
    cleanup_locked_();
    return false;
  };

  try {
    tx_context_ = std::make_shared<TxContext>();
    tx_context_->transport = transport_.get();
    slot_token_ = std::make_shared<SlotToken>();
    if (!claim_process_slot(slot_token_)) return fail();
    slot_claimed_ = true;
    if (!publish_context(tx_context_)) return fail();

    if (!sdk_->create()) return fail();
    sdk_created_ = true;

    if (!sdk_ok_(sdk_->set_hand_type(expected_vendor_model_()),
                 "set_hand_type")) {
      return fail();
    }
    int reported_model = -1;
    if (!sdk_ok_(sdk_->get_hand_type(reported_model), "get_hand_type") ||
        reported_model != expected_vendor_model_()) {
      try {
        logger_->error("LHandPro model readback mismatch: expected={}, got={}",
                       expected_vendor_model_(), reported_model);
      } catch (...) {
      }
      return fail();
    }

    const std::vector<std::uint32_t> response_ids{
        static_cast<std::uint32_t>(0x500 + canfd_node_id_),
        static_cast<std::uint32_t>(0x480 + canfd_node_id_),
        static_cast<std::uint32_t>(0x580 + canfd_node_id_),
        static_cast<std::uint32_t>(0x180 + canfd_node_id_)};
    if (!transport_->open(can_interface_, response_ids)) return fail();
    transport_open_ = true;

    transport_->set_receive_callback([this](const CanFdFrame& frame) {
      if (state_.load(std::memory_order_acquire) == DriverState::Created) {
        return;
      }
      try {
        sdk_ok_(sdk_->decode_canfd(frame.id, frame.data.data(),
                                   static_cast<int>(frame.len)),
                "decode_canfd");
      } catch (...) {
      }
    });

    sdk_->set_send_canfd_callback(&transmit_bridge);
    tx_callback_installed_ = true;

    communication_started_ = true;
    if (!sdk_ok_(sdk_->initial_ex(kCanFdMode, canfd_node_id_), "initial_ex")) {
      return fail();
    }

    sdk_->start_monitor();
    monitor_started_ = true;

    reported_model = -1;
    if (!sdk_ok_(sdk_->get_hand_type(reported_model), "get_hand_type") ||
        reported_model != expected_vendor_model_()) {
      try {
        logger_->error("LHandPro model readback mismatch: expected={}, got={}",
                       expected_vendor_model_(), reported_model);
      } catch (...) {
      }
      return fail();
    }

    int total = 0;
    int active = 0;
    if (!sdk_ok_(sdk_->get_dof(total, active), "get_dof") ||
        total != expected_total_dof_() || active <= 0 || active > total) {
      try {
        logger_->error(
            "LHandPro DOF validation failed: expected total={}, got total={}, "
            "active={}",
            expected_total_dof_(), total, active);
      } catch (...) {
      }
      return fail();
    }
    {
      std::lock_guard<std::mutex> dof_lock(dof_mutex_);
      dof_total_ = total;
      dof_active_ = active;
    }

    if (enable_motors) {
      if (!sdk_ok_(sdk_->set_enable(0, true), "set_enable")) return fail();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (home_motors) {
      if (!sdk_ok_(sdk_->home_motors(0), "home_motors")) return fail();
      std::this_thread::sleep_for(std::chrono::duration<float>(home_wait_time));
    }
    if (!sdk_ok_(sdk_->set_move_no_home(1), "set_move_no_home")) {
      return fail();
    }

    state_.store(DriverState::Ready, std::memory_order_release);
    return true;
  } catch (const std::exception& error) {
    try {
      logger_->error("LHandPro initialization exception: {}", error.what());
    } catch (...) {
    }
    return fail();
  } catch (...) {
    return fail();
  }
}

void LHandProDriver::deinit_hand() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (state_.load(std::memory_order_acquire) == DriverState::Created &&
      !sdk_created_ && !transport_open_ && !tx_callback_installed_ &&
      !communication_started_ && !monitor_started_ && !slot_claimed_ &&
      !tx_context_ && !slot_token_) {
    return;
  }
  cleanup_locked_();
}

void LHandProDriver::cleanup_locked_() noexcept {
  state_.store(DriverState::Stopping, std::memory_order_release);

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (monitor_started_) {
      sdk_->stop_monitor();
      monitor_started_ = false;
    }
  }

  if (transport_open_) transport_->clear_receive_callback();

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (communication_started_) {
      sdk_->close();
      communication_started_ = false;
    }
    if (tx_callback_installed_) {
      sdk_->set_send_canfd_callback(nullptr);
      tx_callback_installed_ = false;
    }
  }

  unpublish_context(tx_context_);

  if (transport_open_) {
    transport_->close();
    transport_open_ = false;
  }

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (sdk_created_) {
      sdk_->destroy();
      sdk_created_ = false;
    }
  }

  {
    std::lock_guard<std::mutex> dof_lock(dof_mutex_);
    dof_total_ = 0;
    dof_active_ = 0;
  }
  tx_context_.reset();
  if (slot_claimed_) release_process_slot(slot_token_);
  slot_claimed_ = false;
  slot_token_.reset();
  state_.store(DriverState::Created, std::memory_order_release);
}

void LHandProDriver::move_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->move_motors(id), "move_motors");
}

void LHandProDriver::stop_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->stop_motors(id), "stop_motors");
}

void LHandProDriver::set_target_position(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) {
    sdk_ok_(sdk_->set_target_position(id, value), "set_target_position");
  }
}

void LHandProDriver::set_target_angle(int id, float value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_target_angle(id, value), "set_target_angle");
}

void LHandProDriver::set_position_velocity(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) {
    sdk_ok_(sdk_->set_position_velocity(id, value), "set_position_velocity");
  }
}

void LHandProDriver::set_max_current(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_max_current(id, value), "set_max_current");
}

void LHandProDriver::set_enable(int id, bool enable) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_enable(id, enable), "set_enable");
}

void LHandProDriver::home_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->home_motors(id), "home_motors");
}

void LHandProDriver::set_move_no_home(int enable) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) {
    sdk_ok_(sdk_->set_move_no_home(enable), "set_move_no_home");
  }
}

void LHandProDriver::clear_alarm(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->clear_alarm(id), "clear_alarm");
}

int LHandProDriver::get_now_position(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_position(id, value), "get_now_position")
             ? value
             : 0;
}

float LHandProDriver::get_now_angle(int id) {
  if (!ready_()) return 0.0F;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0.0F;
  float value = 0.0F;
  return sdk_ok_(sdk_->get_now_angle(id, value), "get_now_angle") ? value
                                                                  : 0.0F;
}

int LHandProDriver::get_now_status(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_status(id, value), "get_now_status") ? value
                                                                    : 0;
}

int LHandProDriver::get_now_current(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_current(id, value), "get_now_current") ? value
                                                                      : 0;
}

int LHandProDriver::get_now_alarm(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_alarm(id, value), "get_now_alarm") ? value : 0;
}
