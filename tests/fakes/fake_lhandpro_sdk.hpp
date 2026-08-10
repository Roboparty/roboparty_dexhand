#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeLHandProSdk final : public LHandProSdk {
 public:
  std::string fail_operation;
  int hand_type{0};
  int total_dof{6};
  int active_dof{6};
  TxCallback tx_callback{nullptr};
  bool created{false};
  bool monitor_started{false};
  int last_mode{-1};
  int last_node{-1};
  std::function<void(const std::string&)> before_call;
  std::function<void()> during_initial_ex;
  int reported_hand_type_override{-1};

  std::vector<std::string> event_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
  }

  int count(const std::string& operation) const {
    const auto copy = event_snapshot();
    return static_cast<int>(std::count(copy.begin(), copy.end(), operation));
  }

  bool create() noexcept override {
    record_("create");
    if (fail_operation == "create") return false;
    created = true;
    return true;
  }

  void destroy() noexcept override {
    if (!created) return;
    record_("destroy");
    created = false;
  }

  int set_hand_type(int type) noexcept override {
    const int code = result_("set_hand_type");
    if (code == 0) hand_type = type;
    return code;
  }

  int get_hand_type(int& type) noexcept override {
    const int code = result_("get_hand_type");
    if (code == 0) {
      type = reported_hand_type_override >= 0 ? reported_hand_type_override
                                              : hand_type;
    }
    return code;
  }

  void set_send_canfd_callback(TxCallback callback) noexcept override {
    record_(callback ? "install_tx" : "clear_tx");
    tx_callback = callback;
  }

  int initial_ex(int mode, int node_id) noexcept override {
    last_mode = mode;
    last_node = node_id;
    try {
      if (during_initial_ex) during_initial_ex();
    } catch (...) {
      return 7;
    }
    return result_("initial_ex");
  }

  void start_monitor() noexcept override {
    record_("start_monitor");
    monitor_started = true;
  }

  void stop_monitor() noexcept override {
    if (!monitor_started) return;
    record_("stop_monitor");
    monitor_started = false;
  }

  void close() noexcept override { record_("close"); }

  int decode_canfd(unsigned int, const unsigned char*, int size) noexcept override {
    last_decode_size = size;
    return result_("decode_canfd");
  }

  int get_dof(int& total, int& active) noexcept override {
    const int code = result_("get_dof");
    if (code == 0) {
      total = total_dof;
      active = active_dof;
    }
    return code;
  }

  int move_motors(int) noexcept override { return result_("move_motors"); }
  int stop_motors(int) noexcept override { return result_("stop_motors"); }

  int set_target_position(int, int) noexcept override {
    return result_("set_target_position");
  }

  int set_target_angle(int, float) noexcept override {
    return result_("set_target_angle");
  }

  int set_position_velocity(int, int) noexcept override {
    return result_("set_position_velocity");
  }

  int set_max_current(int, int) noexcept override {
    return result_("set_max_current");
  }

  int set_enable(int, bool) noexcept override { return result_("set_enable"); }
  int home_motors(int) noexcept override { return result_("home_motors"); }

  int set_move_no_home(int) noexcept override {
    return result_("set_move_no_home");
  }

  int get_now_position(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_position");
  }

  int get_now_angle(int, float& value) noexcept override {
    value = angle_feedback;
    return result_("get_now_angle");
  }

  int get_now_status(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_status");
  }

  int get_now_current(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_current");
  }

  int get_now_alarm(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_alarm");
  }

  int clear_alarm(int) noexcept override { return result_("clear_alarm"); }

  int last_decode_size{-1};
  int int_feedback{123};
  float angle_feedback{12.5F};

 private:
  int result_(const std::string& operation) noexcept {
    record_(operation);
    return fail_operation == operation ? 7 : 0;
  }

  void record_(const std::string& operation) noexcept {
    try {
      if (before_call) before_call(operation);
      std::lock_guard<std::mutex> lock(mutex_);
      events_.push_back(operation);
    } catch (...) {
    }
  }

  mutable std::mutex mutex_;
  std::vector<std::string> events_;
};

}  // namespace roboparty::dexhand::detail
