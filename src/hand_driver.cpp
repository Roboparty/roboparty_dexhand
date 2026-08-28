// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "hand_driver.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>
#include <stdexcept>

// Vendor driver headers (included only here, not in the public header)
#include "drivers/lhandpro/lhandpro_driver.hpp"

namespace {

std::mutex logger_creation_mutex;

std::shared_ptr<spdlog::logger> get_or_create_logger() {
    std::lock_guard<std::mutex> lock(logger_creation_mutex);

    // An externally registered logger must use thread-safe sinks because driver
    // callbacks, lifecycle operations, and public calls can log concurrently.
    if (auto existing = spdlog::get("dexhand")) {
        return existing;
    }

    try {
        return spdlog::stderr_color_mt("dexhand");
    } catch (const spdlog::spdlog_ex&) {
        if (auto existing = spdlog::get("dexhand")) {
            return existing;
        }
        throw;
    }
}

}  // namespace

HandDriver::HandDriver() : logger_(get_or_create_logger()) {}

std::shared_ptr<HandDriver> HandDriver::create_hand(
    const std::string& hand_type,
    const std::string& interface_type,
    const std::string& interface,
    int hand_model,
    int canfd_node_id) {
    if (hand_type != "RP_Hand" && hand_type != "LHandPro") {
        throw std::invalid_argument(
            "Unsupported hand_type: " + hand_type +
            "; supported hand_type is RP_Hand (legacy alias: LHandPro)");
    }
    if (interface_type != "canfd") {
        throw std::invalid_argument("Unsupported interface_type: " +
                                    interface_type);
    }
    if (interface.empty()) {
        throw std::invalid_argument("interface must not be empty");
    }
    if (canfd_node_id < 1 || canfd_node_id > 127) {
        throw std::invalid_argument("canfd_node_id must be in [1, 127], got " +
                                    std::to_string(canfd_node_id));
    }
    if (hand_type == "RP_Hand" && hand_model != HAND_RP_HAND_6DOF) {
        throw std::invalid_argument(
            "Unsupported hand_model: " + std::to_string(hand_model) +
            "; RP_Hand supports HAND_RP_HAND_6DOF (0)");
    }

    using roboparty::dexhand::detail::LHandProModel;
    LHandProModel model;
    switch (hand_model) {
        case HAND_RP_HAND_6DOF:
            model = LHandProModel::Dof6S;
            break;
        case HAND_LHANDPRO_16DOF:
            model = LHandProModel::Dof16;
            break;
        default:
            throw std::invalid_argument(
                "Unsupported hand_model: " + std::to_string(hand_model) +
                "; RP_Hand supports HAND_RP_HAND_6DOF (0)");
    }
    return std::make_shared<LHandProDriver>(interface, model, canfd_node_id);
}
