// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include <spdlog/spdlog.h>
#include <memory>
#include <mutex>
#include <string>

/**
 * @brief Communication transport for dexterous hand.
 */
enum class HandCommType { CANFD = 0 };

/**
 * @brief Dexterous hand models (extensible for future vendors).
 *
 * Values are kept as plain ints so that YAML/python can pass them without
 * referencing vendor-specific headers.
 */
enum HandModel {
    HAND_LHANDPRO_6DOF = 0,
    HAND_LHANDPRO_16DOF = 1,
};

/**
 * @brief Abstract base class for dexterous hand drivers.
 *
 * Architecture mirrors roboparty_motors::MotorDriver:
 *   - Pure virtual interface implemented per vendor.
 *   - Static factory create_hand() dispatches by hand_type string.
 *   - Python bindings (dexhand_py) only bind this base class.
 *
 * Concrete drivers (e.g. LHandProDriver) live in src/drivers/<vendor>/.
 */
class HandDriver {
   public:
    HandDriver();
    virtual ~HandDriver() = default;

    /**
     * @brief Create a supported dexterous-hand driver.
     * @param hand_type Exact vendor name, currently `LHandPro`.
     * @param interface_type Exact transport name, currently `canfd`.
     * @param interface Non-empty Linux SocketCAN interface such as `can0`.
     * @param hand_model Stable public HandModel numeric value.
     * @param canfd_node_id CANopen node ID in the inclusive range 1-127.
     * @return Driver object; construction performs no communication I/O.
     * @throws std::invalid_argument if any configuration value is unsupported.
     */
    static std::shared_ptr<HandDriver> create_hand(
        const std::string& hand_type,
        const std::string& interface_type,
        const std::string& interface,
        int hand_model = HAND_LHANDPRO_6DOF,
        int canfd_node_id = 1);

    // ===== Lifecycle =====

    /**
     * @brief Initialize the hand: create SDK handle, bind CAN callbacks,
     *        optionally enable motors and home.
     *
     * @param enable_motors   Send enable broadcast after init.
     * @param home_motors     Send homing command after enable.
     * @param home_wait_time  Seconds to wait for homing to complete.
     * @return true on success.
     */
    virtual bool init_hand(bool enable_motors = true,
                           bool home_motors = true,
                           float home_wait_time = 5.0) = 0;

    /**
     * @brief Close SDK handle and release CAN resources.
     */
    virtual void deinit_hand() = 0;

    // ===== Motion control =====

    /**
     * @brief Execute motion for all configured target parameters.
     * @param finger_id  Motor/finger ID, 0 = broadcast all.
     */
    virtual void move_motors(int finger_id = 0) = 0;

    /**
     * @brief Stop motor(s) immediately.
     * @param finger_id  0 = broadcast all.
     */
    virtual void stop_motors(int finger_id = 0) = 0;

    // ===== Target setters =====

    /// Set target position in encoder counts.
    virtual void set_target_position(int finger_id, int position) = 0;

    /// Set target angle in degrees.
    virtual void set_target_angle(int finger_id, float angle) = 0;

    /// Set position-mode velocity (counts/sec).
    virtual void set_position_velocity(int finger_id, int velocity) = 0;

    /// Set max current limit (mA).
    virtual void set_max_current(int finger_id, int current) = 0;

    // ===== Enable / Homing =====

    /// Enable or disable motor(s). finger_id=0 broadcasts.
    virtual void set_enable(int finger_id, bool enable) = 0;

    /// Trigger homing. finger_id=0 broadcasts.
    virtual void home_motors(int finger_id = 0) = 0;

    /**
     * @brief Allow motion without homing completion.
     * @param enable  1 = allow, 0 = require homing first.
     */
    virtual void set_move_no_home(int enable) = 0;

    // ===== Status getters (read cached values) =====

    /// Current position in encoder counts.
    virtual int get_now_position(int finger_id) = 0;

    /// Current angle in degrees.
    virtual float get_now_angle(int finger_id) = 0;

    /// Motor status code (vendor-specific).
    virtual int get_now_status(int finger_id) = 0;

    /// Current in mA.
    virtual int get_now_current(int finger_id) = 0;

    /// Alarm code (0 = no alarm).
    virtual int get_now_alarm(int finger_id) = 0;

    /// Clear alarm for a finger. 0 = all.
    virtual void clear_alarm(int finger_id = 0) = 0;

    // ===== Info =====

    /// Get DOF info: total joints and active (motor) DOFs.
    virtual void get_dof(int& total, int& active) {
        std::lock_guard<std::mutex> lock(dof_mutex_);
        total = dof_total_;
        active = dof_active_;
    }

    /// CAN interface name (e.g. "can0").
    virtual std::string get_can_name() { return can_interface_; }

   protected:
    std::shared_ptr<spdlog::logger> logger_;
    std::string can_interface_;
    HandCommType comm_type_{HandCommType::CANFD};
    int canfd_node_id_{1};
    mutable std::mutex dof_mutex_;
    int dof_total_{0};
    int dof_active_{0};
};
