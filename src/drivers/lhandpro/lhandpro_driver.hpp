// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "hand_driver.hpp"
#include "LHandProLib.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_set>

// Forward declaration from roboparty_motors protocol layer
class MotorsCANFD;

/**
 * @brief Dexterous hand driver for LHandPro (RP_Hand) series.
 *
 * This driver wraps the closed-source libLHandProLib.so SDK.
 * The SDK handles all CANopen protocol encoding/decoding internally;
 * our driver's job is purely CAN frame relay:
 *
 *   TX: SDK → send_canfd_callback → MotorsCANFD::transmit → socket
 *   RX: socket → MotorsCANFD receive thread → canfd_data_decode → SDK
 *
 * By using MotorsCANFD::get(interface) (the same singleton as arm motors),
 * arm and hand share ONE socket on the same CAN bus.
 *
 * @note Multi-instance support: unlike a single static pointer, this driver
 *       uses a registry (sdk_handle_ → instance) so multiple hands can coexist
 *       on different CAN buses or different node IDs.
 */
class LHandProDriver : public HandDriver {
   public:
    LHandProDriver(const std::string& interface_type,
                   const std::string& can_interface,
                   int hand_model,
                   int canfd_node_id,
                   int canfd_nom_baudrate,
                   int canfd_dat_baudrate);

    ~LHandProDriver() override;

    // ---- HandDriver interface ----
    bool init_hand(bool enable_motors = true,
                   bool home_motors = true,
                   float home_wait_time = 5.0) override;
    void deinit_hand() override;

    void move_motors(int finger_id = 0) override;
    void stop_motors(int finger_id = 0) override;

    void set_target_position(int finger_id, int position) override;
    void set_target_angle(int finger_id, float angle) override;
    void set_position_velocity(int finger_id, int velocity) override;
    void set_max_current(int finger_id, int current) override;

    void set_enable(int finger_id, bool enable) override;
    void home_motors(int finger_id = 0) override;
    void set_move_no_home(int enable) override;

    int   get_now_position(int finger_id) override;
    float get_now_angle(int finger_id) override;
    int   get_now_status(int finger_id) override;
    int   get_now_current(int finger_id) override;
    int   get_now_alarm(int finger_id) override;
    void  clear_alarm(int finger_id = 0) override;

    void get_dof(int& total, int& active) override;

   private:
    /// Shared CAN bus singleton (same socket as arm motors).
    std::shared_ptr<MotorsCANFD> canfd_;

    /// LHandPro SDK handle (opaque void*).
    lhandprolib_handle sdk_handle_{nullptr};

    /// Whether init_hand() succeeded.
    std::atomic<bool> initialized_{false};

    /// CAN IDs registered for this hand's node (for cleanup on deinit).
    std::unordered_set<uint16_t> registered_can_ids_;

    // ---------------------------------------------------------------
    // Multi-instance send callback bridge
    // ---------------------------------------------------------------
    // The SDK's send callback is a C function pointer (no-capture
    // lambda). To support multiple hand instances simultaneously,
    // we maintain a registry mapping sdk_handle → LHandProDriver*.
    // The no-capture lambda looks up the driver by handle.

    /// Registry: sdk_handle → driver instance (for multi-hand support).
    static std::map<lhandprolib_handle, LHandProDriver*> instance_registry_;
    static std::mutex registry_mutex_;

    /// Lookup driver by SDK handle (used by the static send callback).
    static LHandProDriver* find_instance_(lhandprolib_handle handle);

    /// Setup: create SDK handle, register send/receive callbacks.
    void setup_sdk_callbacks_();

    /// Register receive callback for a specific CAN ID on the shared bus.
    void register_rx_id_(uint16_t can_id);

    /// Compute the CANopen COB-ID for a given base and node_id.
    static constexpr uint16_t cob_id_(uint16_t base, int node_id) {
        return static_cast<uint16_t>(base + node_id);
    }
};
