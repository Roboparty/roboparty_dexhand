// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "hand_driver.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>

// Vendor driver headers (included only here, not in the public header)
#include "drivers/lhandpro/lhandpro_driver.hpp"

HandDriver::HandDriver() {
    // Create or reuse a shared "dexhand" logger
    logger_ = spdlog::get("dexhand");
    if (!logger_) {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_st>();
        logger_ = std::make_shared<spdlog::logger>("dexhand", sink);
        spdlog::register_logger(logger_);
    }
}

std::shared_ptr<HandDriver> HandDriver::create_hand(
    const std::string& hand_type,
    const std::string& interface_type,
    const std::string& interface,
    int hand_model,
    int canfd_node_id,
    int canfd_nom_baudrate,
    int canfd_dat_baudrate) {

    if (hand_type == "LHandPro") {
        return std::make_shared<LHandProDriver>(
            interface_type, interface, hand_model, canfd_node_id,
            canfd_nom_baudrate, canfd_dat_baudrate);
    }

    throw std::runtime_error("Hand type not supported: " + hand_type);
}
