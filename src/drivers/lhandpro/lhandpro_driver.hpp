// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "drivers/lhandpro/lhandpro_feedback_period.hpp"
#include "drivers/lhandpro/lhandpro_sdk.hpp"
#include "hand_driver.hpp"
#include "protocol/canfd_transport.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace roboparty::dexhand::detail {

enum class LHandProModel { Dof6S, Dof16 };
enum class DriverState { Created, Initializing, Ready, Stopping, Faulted };
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
  bool init_for_provisioning();
  void deinit_hand() override;

  roboparty::dexhand::detail::FeedbackPeriodReport show_feedback_period();
  roboparty::dexhand::detail::FeedbackPeriodReport
  apply_feedback_period_20ms();

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
  void check_health() const override;

  roboparty::dexhand::detail::DriverState state_for_test() const noexcept {
    return state_.load(std::memory_order_acquire);
  }

  void set_ready_transition_hook_for_test(std::function<void()> hook) {
    ready_transition_hook_for_test_ = std::move(hook);
  }

  void set_rx_registered_hook_for_test(std::function<void()> hook) {
    rx_registered_hook_for_test_ = std::move(hook);
  }

  void set_rx_entry_attempt_hook_for_test(std::function<void()> hook) {
    rx_entry_attempt_hook_for_test_ = std::move(hook);
  }

  void set_final_commit_hook_for_test(std::function<void()> hook) {
    final_commit_hook_for_test_ = std::move(hook);
  }

  void set_provisioning_pre_lock_hook_for_test(std::function<void()> hook) {
    provisioning_pre_lock_hook_for_test_ = std::move(hook);
  }

  unsigned int pending_rx_callbacks_for_test() const noexcept {
    return pending_rx_callbacks_.load(std::memory_order_acquire);
  }

  bool rx_entry_registration_locked_for_test() noexcept {
    if (!rx_entry_registration_mutex_.try_lock()) return true;
    rx_entry_registration_mutex_.unlock();
    return false;
  }

 private:
  enum class SessionPurpose { Motion, Provisioning };
  enum class FaultSource { Sync, Async, Cleanup };
  struct FaultRecord {
    const char* operation{nullptr};
    int code{0};
    FaultSource source{FaultSource::Sync};
  };
  struct CleanupResult {
    bool failed{false};
    FaultRecord first_failure{};
  };

  CleanupResult cleanup_locked_() noexcept;
  bool init_session_(SessionPurpose purpose, bool enable_motors,
                     bool home_motors, float home_wait_time);
  bool sdk_ok_(int code, const char* operation) const noexcept;
  void record_fault_(const char* operation, int code,
                     FaultSource source) noexcept;
  void record_cleanup_fault_(const char* operation, int code) noexcept;
  bool begin_rx_callback_() noexcept;
  void finish_rx_callback_(bool failed, int code) noexcept;
  bool initialization_healthy_() const noexcept;
  void validate_call_state_(bool allow_faulted,
                            const char* operation) const;
  void validate_provisioning_call_(const char* operation,
                                   std::uint64_t generation) const;
  bool provisioning_epoch_active_(std::uint64_t generation) const noexcept;
  void throw_if_sdk_failed_(int code, const char* operation);
  [[noreturn]] void throw_sticky_fault_() const;
  static const char* fault_source_name_(FaultSource source) noexcept;
  static std::string fault_message_(const FaultRecord& fault);
  std::string fault_report_() const;
  int expected_vendor_model_() const noexcept;
  roboparty::dexhand::detail::ExpectedDof expected_dof_() const noexcept;

  roboparty::dexhand::detail::LHandProModel model_;
  std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk_;
  std::unique_ptr<roboparty::dexhand::detail::CanFdTransport> transport_;
  std::atomic<roboparty::dexhand::detail::DriverState> state_;
  std::mutex lifecycle_mutex_;
  std::mutex rx_entry_registration_mutex_;
  std::mutex rx_init_admission_mutex_;
  std::condition_variable rx_init_admission_cv_;
  std::atomic<unsigned int> pending_rx_callbacks_{0};
  std::atomic<unsigned int> active_rx_callbacks_{0};
  std::function<void()> ready_transition_hook_for_test_;
  std::function<void()> rx_registered_hook_for_test_;
  std::function<void()> rx_entry_attempt_hook_for_test_;
  std::function<void()> final_commit_hook_for_test_;
  std::function<void()> provisioning_pre_lock_hook_for_test_;
  std::mutex sdk_call_mutex_;
  mutable std::mutex health_mutex_;
  std::optional<FaultRecord> first_fault_;
  std::array<FaultRecord, 3> cleanup_faults_{};
  std::size_t cleanup_fault_count_{0};
  bool sdk_created_{false};
  bool transport_open_{false};
  bool tx_callback_installed_{false};
  bool communication_started_{false};
  bool monitor_started_{false};
  bool slot_claimed_{false};
  bool initial_ex_attempted_{false};
  bool safety_cleanup_attempted_{false};
  std::atomic<SessionPurpose> session_purpose_{SessionPurpose::Motion};
  std::atomic<std::uint64_t> session_generation_{0};
  bool safety_cleanup_required_{false};
  std::shared_ptr<roboparty::dexhand::detail::TxContext> tx_context_;
  std::shared_ptr<roboparty::dexhand::detail::SlotToken> slot_token_;
};
