// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

namespace roboparty::dexhand::detail {

class LHandProSdk {
 public:
  using TxCallback = bool (*)(unsigned int, const unsigned char*, unsigned int,
                              int);

  virtual ~LHandProSdk() = default;
  virtual bool create() noexcept = 0;
  virtual void destroy() noexcept = 0;
  virtual int set_hand_type(int type) noexcept = 0;
  virtual int get_hand_type(int& type) noexcept = 0;
  virtual void set_send_canfd_callback(TxCallback callback) noexcept = 0;
  virtual int initial_ex(int mode, int node_id) noexcept = 0;
  virtual void start_monitor() noexcept = 0;
  virtual void stop_monitor() noexcept = 0;
  virtual void close() noexcept = 0;
  virtual int get_sdo_drive_param(unsigned int index, unsigned char subindex,
                                  unsigned int& value) noexcept = 0;
  virtual int set_sdo_drive_param(unsigned int index, unsigned char subindex,
                                  unsigned int value) noexcept = 0;
  virtual int save_sdo_drive_param() noexcept = 0;
  virtual int decode_canfd(unsigned int id, const unsigned char* data,
                           int size) = 0;
  virtual int get_dof(int& total, int& active) noexcept = 0;
  virtual int move_motors(int id) noexcept = 0;
  virtual int stop_motors(int id) noexcept = 0;
  virtual int set_target_position(int id, int position) noexcept = 0;
  virtual int set_target_angle(int id, float angle) noexcept = 0;
  virtual int set_position_velocity(int id, int velocity) noexcept = 0;
  virtual int set_max_current(int id, int current) noexcept = 0;
  virtual int set_enable(int id, bool enable) noexcept = 0;
  virtual int home_motors(int id) noexcept = 0;
  virtual int set_move_no_home(int enable) noexcept = 0;
  virtual int get_now_position(int id, int& value) noexcept = 0;
  virtual int get_now_angle(int id, float& value) noexcept = 0;
  virtual int get_now_status(int id, int& value) noexcept = 0;
  virtual int get_now_current(int id, int& value) noexcept = 0;
  virtual int get_now_alarm(int id, int& value) noexcept = 0;
  virtual int clear_alarm(int id) noexcept = 0;
};

class CapiLHandProSdk final : public LHandProSdk {
 public:
  CapiLHandProSdk() = default;
  CapiLHandProSdk(const CapiLHandProSdk&) = delete;
  CapiLHandProSdk& operator=(const CapiLHandProSdk&) = delete;
  CapiLHandProSdk(CapiLHandProSdk&&) = delete;
  CapiLHandProSdk& operator=(CapiLHandProSdk&&) = delete;
  ~CapiLHandProSdk() override;
  bool create() noexcept override;
  void destroy() noexcept override;
  int set_hand_type(int type) noexcept override;
  int get_hand_type(int& type) noexcept override;
  void set_send_canfd_callback(TxCallback callback) noexcept override;
  int initial_ex(int mode, int node_id) noexcept override;
  void start_monitor() noexcept override;
  void stop_monitor() noexcept override;
  void close() noexcept override;
  int get_sdo_drive_param(unsigned int index, unsigned char subindex,
                          unsigned int& value) noexcept override;
  int set_sdo_drive_param(unsigned int index, unsigned char subindex,
                          unsigned int value) noexcept override;
  int save_sdo_drive_param() noexcept override;
  int decode_canfd(unsigned int id, const unsigned char* data,
                   int size) noexcept override;
  int get_dof(int& total, int& active) noexcept override;
  int move_motors(int id) noexcept override;
  int stop_motors(int id) noexcept override;
  int set_target_position(int id, int position) noexcept override;
  int set_target_angle(int id, float angle) noexcept override;
  int set_position_velocity(int id, int velocity) noexcept override;
  int set_max_current(int id, int current) noexcept override;
  int set_enable(int id, bool enable) noexcept override;
  int home_motors(int id) noexcept override;
  int set_move_no_home(int enable) noexcept override;
  int get_now_position(int id, int& value) noexcept override;
  int get_now_angle(int id, float& value) noexcept override;
  int get_now_status(int id, int& value) noexcept override;
  int get_now_current(int id, int& value) noexcept override;
  int get_now_alarm(int id, int& value) noexcept override;
  int clear_alarm(int id) noexcept override;

 private:
  void* handle_{nullptr};
};

}  // namespace roboparty::dexhand::detail
