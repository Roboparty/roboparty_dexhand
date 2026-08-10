#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <LHandProLib/LHandProLib.h>

namespace roboparty::dexhand::detail {
namespace {

constexpr int kInvalidHandle = -1;

lhandprolib_handle as_handle(void* handle) noexcept {
  return static_cast<lhandprolib_handle>(handle);
}

}  // namespace

CapiLHandProSdk::~CapiLHandProSdk() { destroy(); }

bool CapiLHandProSdk::create() noexcept {
  if (handle_) return true;
  handle_ = lhandprolib_create();
  return handle_ != nullptr;
}

void CapiLHandProSdk::destroy() noexcept {
  if (!handle_) return;
  lhandprolib_destroy(as_handle(handle_));
  handle_ = nullptr;
}

int CapiLHandProSdk::set_hand_type(int type) noexcept {
  return handle_ ? lhandprolib_set_hand_type(as_handle(handle_), type)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_hand_type(int& type) noexcept {
  return handle_ ? lhandprolib_get_hand_type(as_handle(handle_), &type)
                 : kInvalidHandle;
}

void CapiLHandProSdk::set_send_canfd_callback(TxCallback callback) noexcept {
  if (handle_) {
    lhandprolib_set_send_canfd_callback(as_handle(handle_), callback);
  }
}

int CapiLHandProSdk::initial_ex(int mode, int node_id) noexcept {
  return handle_
             ? lhandprolib_initial_ex(as_handle(handle_), mode, node_id)
             : kInvalidHandle;
}

void CapiLHandProSdk::start_monitor() noexcept {
  if (handle_) lhandprolib_start_monitor(as_handle(handle_));
}

void CapiLHandProSdk::stop_monitor() noexcept {
  if (handle_) lhandprolib_stop_monitor(as_handle(handle_));
}

void CapiLHandProSdk::close() noexcept {
  if (handle_) lhandprolib_close(as_handle(handle_));
}

int CapiLHandProSdk::decode_canfd(unsigned int id,
                                  const unsigned char* data,
                                  int size) noexcept {
  return handle_ ? lhandprolib_set_canfd_data_decode(as_handle(handle_), id,
                                                      data, size)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_dof(int& total, int& active) noexcept {
  return handle_ ? lhandprolib_get_dof(as_handle(handle_), &total, &active)
                 : kInvalidHandle;
}

int CapiLHandProSdk::move_motors(int id) noexcept {
  return handle_ ? lhandprolib_move_motors(as_handle(handle_), id)
                 : kInvalidHandle;
}

int CapiLHandProSdk::stop_motors(int id) noexcept {
  return handle_ ? lhandprolib_stop_motors(as_handle(handle_), id)
                 : kInvalidHandle;
}

int CapiLHandProSdk::set_target_position(int id, int value) noexcept {
  return handle_
             ? lhandprolib_set_target_position(as_handle(handle_), id, value)
             : kInvalidHandle;
}

int CapiLHandProSdk::set_target_angle(int id, float value) noexcept {
  return handle_ ? lhandprolib_set_target_angle(as_handle(handle_), id, value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::set_position_velocity(int id, int value) noexcept {
  return handle_
             ? lhandprolib_set_position_velocity(as_handle(handle_), id, value)
             : kInvalidHandle;
}

int CapiLHandProSdk::set_max_current(int id, int value) noexcept {
  return handle_ ? lhandprolib_set_max_current(as_handle(handle_), id, value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::set_enable(int id, bool enable) noexcept {
  return handle_ ? lhandprolib_set_enable(as_handle(handle_), id,
                                           enable ? 1 : 0)
                 : kInvalidHandle;
}

int CapiLHandProSdk::home_motors(int id) noexcept {
  return handle_ ? lhandprolib_home_motors(as_handle(handle_), id)
                 : kInvalidHandle;
}

int CapiLHandProSdk::set_move_no_home(int enable) noexcept {
  return handle_ ? lhandprolib_set_move_no_home(as_handle(handle_), enable)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_now_position(int id, int& value) noexcept {
  return handle_
             ? lhandprolib_get_now_position(as_handle(handle_), id, &value)
             : kInvalidHandle;
}

int CapiLHandProSdk::get_now_angle(int id, float& value) noexcept {
  return handle_ ? lhandprolib_get_now_angle(as_handle(handle_), id, &value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_now_status(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_status(as_handle(handle_), id, &value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_now_current(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_current(as_handle(handle_), id, &value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::get_now_alarm(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_alarm(as_handle(handle_), id, &value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::clear_alarm(int id) noexcept {
  return handle_ ? lhandprolib_set_clear_alarm(as_handle(handle_), id)
                 : kInvalidHandle;
}

}  // namespace roboparty::dexhand::detail
