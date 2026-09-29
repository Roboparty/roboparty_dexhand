#include <dh116s/hand.hpp>
#include "../src/feedback_format.hpp"
#include <iostream>

int main() {
    dh116s::Hand hand;
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    expect(dh116s::detail::valid_feedback(0x50, 8, 64), "captured 8-slot position frame");
    expect(dh116s::detail::valid_feedback(0x5a, 31, 64), "captured 31-slot status frame");
    expect(dh116s::detail::valid_feedback(0x50, 6, 48), "six-slot position frame");
    expect(dh116s::detail::valid_feedback(0x5a, 6, 16), "six-slot status frame");
    expect(!dh116s::detail::valid_feedback(0x50, 8, 48), "truncated position frame");
    expect(!dh116s::detail::valid_feedback(0x5a, 31, 16), "truncated status frame");
    expect(!dh116s::detail::valid_feedback(0x50, 5, 64), "missing required axes");
    expect(!hand.is_connected() && !hand.get_state().feedback_valid, "initial state invalid");
    expect(!hand.connect("", 1), "empty interface rejected");
    expect(!hand.connect("can0", 0), "invalid node rejected");
    expect(!hand.connect("can0", 1, 0), "invalid retry count rejected");
    expect(!hand.set_position(0, 0), "broadcast not public");
    expect(!hand.set_positions({0, 0, 0, 0, 0, 10001}), "position range");
    expect(!hand.set_speed(0) && !hand.set_current_limit(3001), "parameter ranges");
    expect(!hand.enable() && !hand.home() && !hand.stop() && !hand.disable(), "offline operations fail");
    hand.disconnect();
    hand.disconnect();
    // A deliberately overlong name cannot refer to a Linux network interface.
    for (int attempt = 0; attempt < 2; ++attempt) {
        expect(!hand.connect("dh116s_nonexistent_interface", 1, 1), "failed connect releases worker/owner");
        expect(!hand.is_connected() && !hand.get_state().feedback_valid, "failed connection state");
        hand.disconnect();
    }
    std::cout << "Basic smoke failures: " << failures << '\n';
    return failures ? 1 : 0;
}
