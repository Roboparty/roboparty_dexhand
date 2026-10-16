#include <rphand/hand.hpp>
#include "../src/rphand/protocol.hpp"
#include <iostream>
#include <type_traits>

static_assert(std::is_same_v<decltype(&rphand::Hand::connect),
    bool (rphand::Hand::*)(const std::string&, int)>);

int main() {
    rphand::Hand hand;
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    expect(rphand::valid_feedback(0x50, 8, 64), "captured 8-slot position frame");
    expect(rphand::valid_feedback(0x5a, 31, 64), "captured 31-slot status frame");
    expect(rphand::valid_feedback(0x50, 6, 56), "six-axis position frame with fixed status offset");
    expect(rphand::valid_feedback(0x5a, 6, 16), "six-slot status frame");
    expect(!rphand::valid_feedback(0x50, 6, 48), "missing trailing position status bytes");
    expect(!rphand::valid_feedback(0x50, 8, 48), "truncated position frame");
    expect(!rphand::valid_feedback(0x5a, 31, 16), "truncated status frame");
    expect(!rphand::valid_feedback(0x50, 5, 64), "missing required axes");
    expect(!hand.is_connected() && !hand.get_state().feedback_valid, "initial state invalid");
    expect(!hand.connect("", 1), "empty interface rejected");
    expect(!hand.connect("can0", 0), "invalid node rejected");
    expect(!hand.set_position(0, 0), "broadcast not public");
    expect(!hand.set_positions({0, 0, 0, 0, 0, 10001}), "position range");
    expect(!hand.set_speed(0) && !hand.set_current_limit(3001), "parameter ranges");
    expect(!hand.enable() && !hand.home() && !hand.stop() && !hand.disable(), "offline operations fail");
    expect(!hand.prepare_disconnect(), "offline cleanup cannot confirm device state");
    hand.disconnect();
    hand.disconnect();
    // A deliberately overlong name cannot refer to a Linux network interface.
    for (int attempt = 0; attempt < 2; ++attempt) {
        expect(!hand.connect("rphand_nonexistent_interface", 1), "failed connect releases worker/owner");
        expect(!hand.is_connected() && !hand.get_state().feedback_valid, "failed connection state");
        hand.disconnect();
    }
    std::cout << "Basic smoke failures: " << failures << '\n';
    return failures ? 1 : 0;
}
