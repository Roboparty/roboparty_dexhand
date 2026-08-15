// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"
#include "hand_driver.hpp"
#include "protocol/canfd_transport.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace roboparty::dexhand::detail {

enum class LHandProModel { Dof6S, Dof16 };
enum class DriverState { Created, Initializing, Ready, Stopping };
struct ExpectedDof {
  int total;
  int active;
};
struct TxContext;
struct SlotToken;

}  // namespace roboparty::dexhand::detail

/**
 * @brief LHandPro CAN-FD driver with transaction-safe resource ownership.
 *
 * The vendor transmit callback has no handle or user-data parameter, so one
 * LHandPro driver may own the process-wide callback route at a time.
 */
class LHandProDriver final : public HandDriver {
 public:
  LHandProDriver(std::string can_interface,
                 roboparty::dexhand::detail::LHandProModel model,
                 int canfd_node_id);
  LHandProDriver(
      std::string can_interface,
      roboparty::dexhand::detail::LHandProModel model,
      int canfd_node_id,
      std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk,
      std::unique_ptr<roboparty::dexhand::detail::CanFdTransport> transport);
  ~LHandProDriver() override;

  bool init_hand(bool enable_motors, bool home_motors,
                 float home_wait_time) override;
  void deinit_hand() override;

  void move_motors(int finger_id) override;
  void stop_motors(int finger_id) override;
  void set_target_position(int finger_id, int position) override;
  void set_target_angle(int finger_id, float angle) override;
  void set_position_velocity(int finger_id, int velocity) override;
  void set_max_current(int finger_id, int current) override;
  void set_enable(int finger_id, bool enable) override;
  void home_motors(int finger_id) override;
  void set_move_no_home(int enable) override;
  int get_now_position(int finger_id) override;
  float get_now_angle(int finger_id) override;
  int get_now_status(int finger_id) override;
  int get_now_current(int finger_id) override;
  int get_now_alarm(int finger_id) override;
  void clear_alarm(int finger_id) override;

  roboparty::dexhand::detail::DriverState state_for_test() const noexcept {
    return state_.load(std::memory_order_acquire);
  }

 private:
  void cleanup_locked_() noexcept;
  bool sdk_ok_(int code, const char* operation) const noexcept;
  bool ready_() const noexcept;
  int expected_vendor_model_() const noexcept;
  roboparty::dexhand::detail::ExpectedDof expected_dof_() const noexcept;

  roboparty::dexhand::detail::LHandProModel model_;
  std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk_;
  std::unique_ptr<roboparty::dexhand::detail::CanFdTransport> transport_;
  std::atomic<roboparty::dexhand::detail::DriverState> state_;
  std::mutex lifecycle_mutex_;
  std::mutex sdk_call_mutex_;
  bool sdk_created_{false};
  bool transport_open_{false};
  bool tx_callback_installed_{false};
  bool communication_started_{false};
  bool monitor_started_{false};
  bool slot_claimed_{false};
  std::shared_ptr<roboparty::dexhand::detail::TxContext> tx_context_;
  std::shared_ptr<roboparty::dexhand::detail::SlotToken> slot_token_;
};
