// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeLHandProSdk final : public LHandProSdk {
 public:
  struct SdoAccess {
    unsigned int index;
    unsigned char subindex;
    unsigned int value;
  };

  FakeLHandProSdk() {
    for (const unsigned int index : {0x201DU, 0x205DU, 0x209DU, 0x20DDU,
                                     0x211DU, 0x215DU}) {
      sdo_values_.emplace(index, 200U);
    }
  }

  std::string fail_operation;
  int failure_code{7};
  int hand_type{0};
  int total_dof{11};
  int active_dof{6};
  TxCallback tx_callback{nullptr};
  bool created{false};
  bool monitor_started{false};
  int last_mode{-1};
  int last_node{-1};
  std::function<void(const std::string&)> before_call;
  std::function<void()> during_initial_ex;
  int reported_hand_type_override{-1};
  bool throw_decode_exception{false};

  std::vector<std::string> event_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
  }

  int count(const std::string& operation) const {
    const auto copy = event_snapshot();
    return static_cast<int>(std::count(copy.begin(), copy.end(), operation));
  }

  int count_set_enable(bool enable) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(
        set_enable_arguments_.begin(), set_enable_arguments_.end(),
        [enable](const auto& argument) { return argument.second == enable; }));
  }

  int count_set_enable(int id, bool enable) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count(
        set_enable_arguments_.begin(), set_enable_arguments_.end(),
        std::make_pair(id, enable)));
  }

  int count_set_move_no_home(int enable) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(
        std::count(set_move_no_home_arguments_.begin(),
                   set_move_no_home_arguments_.end(), enable));
  }

  int count_stop_motors(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count(stop_motors_arguments_.begin(),
                                       stop_motors_arguments_.end(), id));
  }

  void script_result(const std::string& operation, int code) {
    std::lock_guard<std::mutex> lock(mutex_);
    scripted_results_[operation].push_back(code);
  }

  void clear_scripts() {
    std::lock_guard<std::mutex> lock(mutex_);
    scripted_results_.clear();
  }

  void set_sdo_value(unsigned int index, unsigned int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    sdo_values_[index] = value;
  }

  std::vector<SdoAccess> sdo_read_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdo_reads_;
  }

  std::vector<SdoAccess> sdo_write_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdo_writes_;
  }

  std::vector<SdoAccess> sdo_read_attempt_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdo_read_attempts_;
  }

  std::vector<SdoAccess> sdo_write_attempt_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdo_write_attempts_;
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

  int get_sdo_drive_param(unsigned int index, unsigned char subindex,
                          unsigned int& value) noexcept override {
    record_sdo_read_attempt_(index, subindex);
    const int code = result_("get_sdo_drive_param");
    if (code != 0) return code;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (subindex != 0x14U) return failure_code;
      const auto found = sdo_values_.find(index);
      if (found == sdo_values_.end()) return failure_code;
      value = found->second;
      sdo_reads_.push_back({index, subindex, value});
      return 0;
    } catch (...) {
      return failure_code;
    }
  }

  int set_sdo_drive_param(unsigned int index, unsigned char subindex,
                          unsigned int value) noexcept override {
    record_sdo_write_attempt_(index, subindex, value);
    const int code = result_("set_sdo_drive_param");
    if (code != 0) return code;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (subindex != 0x14U) return failure_code;
      const auto found = sdo_values_.find(index);
      if (found == sdo_values_.end()) return failure_code;
      sdo_writes_.push_back({index, subindex, value});
      found->second = value;
      return 0;
    } catch (...) {
      return failure_code;
    }
  }

  int save_sdo_drive_param() noexcept override {
    return result_("save_sdo_drive_param");
  }

  int decode_canfd(unsigned int, const unsigned char*, int size) override {
    last_decode_size = size;
    if (throw_decode_exception) {
      record_("decode_canfd");
      throw std::runtime_error("decode failure");
    }
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
  int stop_motors(int id) noexcept override {
    last_stop_id = id;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_motors_arguments_.push_back(id);
    }
    return result_("stop_motors", "stop_motors:" + std::to_string(id));
  }

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

  int set_enable(int id, bool enable) noexcept override {
    last_enable_id = id;
    last_enable = enable;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      set_enable_arguments_.emplace_back(id, enable);
    } catch (...) {
    }
    return result_("set_enable",
                   enable ? "set_enable:true" : "set_enable:false");
  }
  int home_motors(int) noexcept override { return result_("home_motors"); }

  int set_move_no_home(int enable) noexcept override {
    last_move_no_home = enable;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      set_move_no_home_arguments_.push_back(enable);
    } catch (...) {
    }
    return result_("set_move_no_home",
                   "set_move_no_home:" + std::to_string(enable));
  }

  int get_now_position(int, int& value) noexcept override {
    value = position_feedback.value_or(int_feedback);
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
  int last_stop_id{-1};
  int last_enable_id{-1};
  bool last_enable{false};
  int last_move_no_home{-1};
  int int_feedback{123};
  std::optional<int> position_feedback;
  float angle_feedback{12.5F};

 private:
  int result_(const std::string& operation,
              const std::string& qualified_operation = {}) noexcept {
    record_(operation);
    return scripted_result_(operation, qualified_operation);
  }

  int scripted_result_(const std::string& operation,
                       const std::string& qualified_operation) noexcept {
    int outcome = 0;
    bool scripted = false;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      auto take = [&](const std::string& key) {
        const auto found = scripted_results_.find(key);
        if (found == scripted_results_.end() || found->second.empty()) {
          return false;
        }
        outcome = found->second.front();
        found->second.pop_front();
        if (found->second.empty()) scripted_results_.erase(found);
        return true;
      };
      if (!qualified_operation.empty()) scripted = take(qualified_operation);
      if (!scripted) scripted = take(operation);
    } catch (...) {
      return fail_operation == operation ? failure_code : 0;
    }
    if (scripted) return outcome;
    return fail_operation == operation ? failure_code : 0;
  }

  void record_(const std::string& operation) noexcept {
    try {
      if (before_call) before_call(operation);
      std::lock_guard<std::mutex> lock(mutex_);
      events_.push_back(operation);
    } catch (...) {
    }
  }

  void record_sdo_read_attempt_(unsigned int index,
                                unsigned char subindex) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      sdo_read_attempts_.push_back({index, subindex, 0U});
    } catch (...) {
    }
  }

  void record_sdo_write_attempt_(unsigned int index, unsigned char subindex,
                                 unsigned int value) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      sdo_write_attempts_.push_back({index, subindex, value});
    } catch (...) {
    }
  }

  mutable std::mutex mutex_;
  std::vector<std::string> events_;
  std::vector<int> stop_motors_arguments_;
  std::vector<std::pair<int, bool>> set_enable_arguments_;
  std::vector<int> set_move_no_home_arguments_;
  std::unordered_map<unsigned int, unsigned int> sdo_values_;
  std::vector<SdoAccess> sdo_reads_;
  std::vector<SdoAccess> sdo_writes_;
  std::vector<SdoAccess> sdo_read_attempts_;
  std::vector<SdoAccess> sdo_write_attempts_;
  std::unordered_map<std::string, std::deque<int>> scripted_results_;
};

}  // namespace roboparty::dexhand::detail
