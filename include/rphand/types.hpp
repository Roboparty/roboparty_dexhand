#pragma once

#include <array>
#include <string>

namespace rphand {

enum class Backend { SocketCan, GsUsbWinUsb };

struct ConnectionOptions {
    Backend backend = Backend::SocketCan;
    std::string interface = "can0"; // Linux interface, already configured and UP.
    std::string serial;    // Windows: exact USB serial, required.
    int channel = 0;
    int node_id = 1;
    int bitrate = 1000000; // Product default: 1 Mbps arbitration.
    int data_bitrate = 5000000; // Product default: 5 Mbps data, BRS enabled.
};

// Order: thumb abduction, thumb flexion, index, middle, ring, little.
struct HandState {
    std::array<int, 6> positions{};
    std::array<int, 6> velocities{};
    std::array<int, 6> currents{};
    std::array<bool, 6> enabled{};
    std::array<bool, 6> homed{};
    std::array<bool, 6> torque_reached{};
    std::array<int, 6> fault_codes{};
    bool feedback_valid = false;
};

}  // namespace rphand
