#pragma once

#include <rphand/types.hpp>
#include <charconv>
#include <limits>
#include <string>

namespace example {
struct Options {
    rphand::ConnectionOptions connection;
    int amplitude = 6000;
    bool connect_only = false;
};

inline constexpr const char* connection_help =
    "Windows: --serial SERIAL [--node-id 1]\n"
    "Linux:   [--interface can0] [--node-id 1]\n"
    "Product CAN FD rates: 1000000 / 5000000 bit/s.\n"
    "Advanced USB overrides: --bitrate RATE --data-bitrate RATE.\n"
    "Legacy Linux alias: --side left|right selects can0|can3.\n";

inline bool parse(int argc, char** argv, Options& options, bool wave = true) {
    bool usb = false, interface = false, side = false, rates = false;
    auto integer = [](const std::string& value, int& output, int maximum) {
        const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size()
            && output >= 1 && output <= maximum;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (wave && arg == "--connect-only") { options.connect_only = true; continue; }
        if (i + 1 >= argc) return false;
        const std::string value = argv[++i];
        if (arg == "--serial") {
            if (value.empty() || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                return false;
            options.connection.serial = value;
            usb = true;
        } else if (arg == "--interface") {
            if (value.empty()) return false;
            options.connection.interface = value;
            interface = true;
        } else if (arg == "--side") {
            if (value != "left" && value != "right") return false;
            options.connection.interface = value == "left" ? "can0" : "can3";
            side = true;
        } else if (arg == "--node-id") {
            if (!integer(value, options.connection.node_id, 127)) return false;
        } else if (wave && arg == "--amplitude") {
            if (!integer(value, options.amplitude, 10000)) return false;
        } else if (arg == "--bitrate" || arg == "--data-bitrate") {
            int& rate = arg == "--bitrate" ? options.connection.bitrate : options.connection.data_bitrate;
            if (!integer(value, rate, std::numeric_limits<int>::max())) return false;
            rates = true;
        } else return false;
    }
    if ((interface && side) || (usb && (interface || side))) return false;
#ifdef _WIN32
    if (!usb) return false;
    options.connection.backend = rphand::Backend::GsUsbWinUsb;
#else
    if (usb || rates) return false;
#endif
    // On Windows rates are optional; on Linux the OS owns bus configuration.
    (void)rates;
    return true;
}
}  // namespace example
