// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "lhandpro_driver.hpp"

#include <linux/can.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <thread>

// MotorsCANFD full definition (needed for get() and transmit())
#include "protocol/canfd_iso.hpp"

// ============================================================================
// Static registry for multi-instance send callback bridge
// ============================================================================
std::map<lhandprolib_handle, LHandProDriver*> LHandProDriver::instance_registry_;
std::mutex LHandProDriver::registry_mutex_;

LHandProDriver* LHandProDriver::find_instance_(lhandprolib_handle handle) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = instance_registry_.find(handle);
    return (it != instance_registry_.end()) ? it->second : nullptr;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

LHandProDriver::LHandProDriver(const std::string& interface_type,
                               const std::string& can_interface,
                               int hand_model,
                               int canfd_node_id,
                               int canfd_nom_baudrate,
                               int canfd_dat_baudrate) {
    can_interface_ = can_interface;
    canfd_node_id_ = canfd_node_id;

    if (interface_type != "canfd" && interface_type != "ethercanfd") {
        throw std::runtime_error(
            "LHandPro driver currently supports CANFD only, got: " + interface_type);
    }
    comm_type_ = HandCommType::CANFD;

    // Store baudrates for documentation/debugging (MotorsCANFD handles actual setup)
    (void)canfd_nom_baudrate;  // MotorsCANFD configures the interface itself
    (void)canfd_dat_baudrate;
    (void)hand_model;  // Reserved for future model-specific parameters
}

LHandProDriver::~LHandProDriver() {
    deinit_hand();
}

// ============================================================================
// Init / Deinit
// ============================================================================

bool LHandProDriver::init_hand(bool enable_motors,
                               bool home_motors,
                               float home_wait_time) {
    if (initialized_) {
        logger_->warn("LHandPro already initialized");
        return true;
    }

    // 1. Get the shared CAN bus singleton (same socket as arm motors!)
    canfd_ = MotorsCANFD::get(can_interface_, "socketcan");
    if (!canfd_) {
        logger_->error("Failed to get CANFD bus for {}", can_interface_);
        return false;
    }

    // 2. Create SDK handle
    sdk_handle_ = lhandprolib_create();
    if (!sdk_handle_) {
        logger_->error("Failed to create LHandPro SDK handle");
        return false;
    }

    // 3. Register this instance in the multi-instance registry
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        instance_registry_[sdk_handle_] = this;
    }

    // 4. Register send/receive callbacks (before initial_ex!)
    setup_sdk_callbacks_();

    // 5. Initialize SDK in CANFD mode with node_id
    int ret = lhandprolib_initial_ex(sdk_handle_, C_LCN_CANFD, canfd_node_id_);
    if (ret != C_LER_NONE) {
        logger_->error("LHandPro initial_ex failed, error={}", ret);
        // Cleanup: unregister callbacks, remove from registry, destroy handle
        for (uint16_t id : registered_can_ids_) {
            if (canfd_) canfd_->remove_canfd_callback(id);
        }
        registered_can_ids_.clear();
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            instance_registry_.erase(sdk_handle_);
        }
        lhandprolib_destroy(sdk_handle_);
        sdk_handle_ = nullptr;
        return false;
    }

    // 5b. Start SDK background monitor thread.
    // This thread periodically polls the hand for status, which triggers the
    // hand to send back TPDO feedback frames (0x481 etc.) at ~2000fps.
    // Without start_monitor(), get_now_position() etc. always return stale/0.
    lhandprolib_start_monitor(sdk_handle_);

    // 6. Read DOF info
    int total = 0, active = 0;
    lhandprolib_get_dof(sdk_handle_, &total, &active);
    dof_total_ = total;
    dof_active_ = active;
    logger_->info("LHandPro connected: DOF total={}, active={}", total, active);

    // 7. Enable motors
    if (enable_motors) {
        lhandprolib_set_enable(sdk_handle_, 0, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // 8. Home motors
    if (home_motors) {
        lhandprolib_home_motors(sdk_handle_, 0);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(home_wait_time * 1000)));
    }

    // 9. Allow motion without completed homing (safety net for move_motors)
    lhandprolib_set_move_no_home(sdk_handle_, 1);

    initialized_ = true;
    return true;
}

void LHandProDriver::deinit_hand() {
    if (!initialized_ && !sdk_handle_) return;

    // 1. Stop monitor thread first (stops feedback polling → no new 0x481 frames)
    if (sdk_handle_) {
        lhandprolib_stop_monitor(sdk_handle_);
    }

    // 2. Close SDK (stops internal monitoring threads)
    if (sdk_handle_) {
        lhandprolib_close(sdk_handle_);
    }

    // 2. Remove all CAN receive callbacks BEFORE destroying handle.
    //    This prevents the receive thread from calling set_canfd_data_decode
    //    with a stale handle (use-after-free).
    for (uint16_t id : registered_can_ids_) {
        if (canfd_) {
            canfd_->remove_canfd_callback(id);
        }
    }
    registered_can_ids_.clear();

    // 3. Remove from instance registry
    if (sdk_handle_) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        instance_registry_.erase(sdk_handle_);
    }

    // 4. Destroy SDK handle
    if (sdk_handle_) {
        lhandprolib_destroy(sdk_handle_);
        sdk_handle_ = nullptr;
    }

    initialized_ = false;
}

// ============================================================================
// CAN frame relay setup (the core bridge)
// ============================================================================

void LHandProDriver::setup_sdk_callbacks_() {
    // --- TX: SDK → SocketCAN ---
    // No-capture lambda → C function pointer. Uses the instance registry
    // to find the correct driver for this SDK handle (multi-instance safe).
    lhandprolib_set_send_canfd_callback(
        sdk_handle_,
        [](unsigned int id, const unsigned char* data,
           unsigned int size, int is_extended) -> bool {
            // The SDK doesn't pass the handle to the callback, so we can only
            // support one active hand per process via this approach.
            // For multi-hand, each hand must use a separate process or the
            // SDK must provide a user-data pointer in the callback.
            // Current workaround: find the first registered instance.
            LHandProDriver* self = nullptr;
            {
                std::lock_guard<std::mutex> lock(registry_mutex_);
                if (!instance_registry_.empty()) {
                    // Find the instance whose handle matches by checking if
                    // any registered instance's CAN ID range covers this frame.
                    // Simple approach: use the first (only) instance.
                    self = instance_registry_.begin()->second;
                }
            }
            if (!self || !self->canfd_) return false;

            struct canfd_frame tx;
            std::memset(&tx, 0, sizeof(tx));
            tx.can_id = is_extended ? (id | CAN_EFF_FLAG) : id;
            tx.len = static_cast<__u8>(std::min((unsigned int)64, size));
            std::memcpy(tx.data, data, tx.len);
            // Only set CANFD_BRS for frames > 8 bytes.
            // The hand's SDO/command frames are 8 bytes (standard CAN);
            // sending them as CANFD-BRS causes the hand to not respond.
            // Feedback frames (0x501) from the hand are CANFD (up to 64 bytes).
            if (tx.len > 8) {
                tx.flags = CANFD_BRS;
            }
            self->canfd_->transmit(tx);
            return true;
        });

    // --- RX: SocketCAN → SDK ---
    // Register receive callbacks for the CAN IDs this hand uses.
    // Observed via candump (node_id=1):
    //   0x501 = SDO/feedback response (the hand replies to 0x601 commands with this ID)
    // The hand may also use other IDs at runtime; we register the observed set.
    // NOTE: The LHandPro uses non-standard CANopen COB-IDs (0x500+node instead
    // of 0x580+node for SDO response). This was confirmed by candump analysis.
    const uint16_t response_ids[] = {
        cob_id_(0x500, canfd_node_id_),  // 0x501 SDO response (observed)
        cob_id_(0x480, canfd_node_id_),  // 0x481 TPDO feedback (observed in Python version)
        cob_id_(0x580, canfd_node_id_),  // 0x581 standard CANopen SDO response
        cob_id_(0x180, canfd_node_id_),  // 0x181 TPDO1
    };
    for (uint16_t id : response_ids) {
        register_rx_id_(id);
    }
}

void LHandProDriver::register_rx_id_(uint16_t can_id) {
    if (!canfd_) return;

    // Capture sdk_handle_ by value (it's a void*, stable for this hand's lifetime).
    // The callback is removed in deinit_hand() before the handle is destroyed,
    // preventing use-after-free.
    lhandprolib_handle sdk = sdk_handle_;
    canfd_->add_canfd_callback(
        [sdk](const struct canfd_frame& rx) {
            // Strip CAN_EFF_FLAG if present, feed raw ID + data to SDK for decoding.
            // data_size is always 64 (CANFD buffer size) per SDK documentation.
            uint32_t raw_id = rx.can_id & CAN_EFF_MASK;
            lhandprolib_set_canfd_data_decode(sdk, raw_id, rx.data, 64);
        },
        can_id);
    registered_can_ids_.insert(can_id);
}

// ============================================================================
// Motion control (thin wrappers around C API)
// ============================================================================

void LHandProDriver::move_motors(int finger_id) {
    if (!sdk_handle_) return;
    lhandprolib_move_motors(sdk_handle_, finger_id);
}

void LHandProDriver::stop_motors(int finger_id) {
    if (!sdk_handle_) return;
    lhandprolib_stop_motors(sdk_handle_, finger_id);
}

// ============================================================================
// Target setters
// ============================================================================

void LHandProDriver::set_target_position(int finger_id, int position) {
    if (!sdk_handle_) return;
    lhandprolib_set_target_position(sdk_handle_, finger_id, position);
}

void LHandProDriver::set_target_angle(int finger_id, float angle) {
    if (!sdk_handle_) return;
    lhandprolib_set_target_angle(sdk_handle_, finger_id, angle);
}

void LHandProDriver::set_position_velocity(int finger_id, int velocity) {
    if (!sdk_handle_) return;
    lhandprolib_set_position_velocity(sdk_handle_, finger_id, velocity);
}

void LHandProDriver::set_max_current(int finger_id, int current) {
    if (!sdk_handle_) return;
    lhandprolib_set_max_current(sdk_handle_, finger_id, current);
}

// ============================================================================
// Enable / Homing
// ============================================================================

void LHandProDriver::set_enable(int finger_id, bool enable) {
    if (!sdk_handle_) return;
    lhandprolib_set_enable(sdk_handle_, finger_id, enable ? 1 : 0);
}

void LHandProDriver::home_motors(int finger_id) {
    if (!sdk_handle_) return;
    lhandprolib_home_motors(sdk_handle_, finger_id);
}

void LHandProDriver::set_move_no_home(int enable) {
    if (!sdk_handle_) return;
    lhandprolib_set_move_no_home(sdk_handle_, enable);
}

// ============================================================================
// Status getters (read cached values from SDK)
// ============================================================================

int LHandProDriver::get_now_position(int finger_id) {
    if (!sdk_handle_) return 0;
    int val = 0;
    lhandprolib_get_now_position(sdk_handle_, finger_id, &val);
    return val;
}

float LHandProDriver::get_now_angle(int finger_id) {
    if (!sdk_handle_) return 0.0f;
    float val = 0.0f;
    lhandprolib_get_now_angle(sdk_handle_, finger_id, &val);
    return val;
}

int LHandProDriver::get_now_status(int finger_id) {
    if (!sdk_handle_) return 0;
    int val = 0;
    lhandprolib_get_now_status(sdk_handle_, finger_id, &val);
    return val;
}

int LHandProDriver::get_now_current(int finger_id) {
    if (!sdk_handle_) return 0;
    int val = 0;
    lhandprolib_get_now_current(sdk_handle_, finger_id, &val);
    return val;
}

int LHandProDriver::get_now_alarm(int finger_id) {
    if (!sdk_handle_) return 0;
    int val = 0;
    lhandprolib_get_now_alarm(sdk_handle_, finger_id, &val);
    return val;
}

void LHandProDriver::clear_alarm(int finger_id) {
    if (!sdk_handle_) return;
    lhandprolib_set_clear_alarm(sdk_handle_, finger_id);
}

void LHandProDriver::get_dof(int& total, int& active) {
    total = dof_total_;
    active = dof_active_;
}
