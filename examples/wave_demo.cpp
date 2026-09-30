#include <dh116s/hand.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
volatile std::sig_atomic_t interrupted = 0;
void signal_handler(int) { interrupted = 1; }
struct Options {
    std::array<std::string, 2> can{"can0", "can3"};
    std::array<int, 2> node{1, 1};
    // LHandProLib 20260921 dry-run initialization defaults, verified on both architectures.
    int speed = 20000, current = 400, amplitude = 6000;
    int period_ms = 3000, cycles = 10;
    int rate = 200;
    bool dry_run = false;
    bool connect_only = false;
};
const char* help =
    "Usage: dh116s_wave_demo [--left can0 1] [--right can3 1]\n"
    "  --speed 20000      Stroke units/second (vendor SDK default)\n"
    "  --current 400      Current limit, per mille (vendor SDK default)\n"
    "  --amplitude 6000   Four-finger position range 0..amplitude (1..10000)\n"
    "  --period-ms 3000   Wave period (1000..60000 ms)\n"
    "  --cycles 10       Number of wave cycles (1..100)\n"
    "  --rate 200        Target update rate: 50, 100, 150 or 200 Hz\n"
    "  --dry-run         Print targets without opening CAN or waiting\n"
    "  --connect-only    Configure/check feedback, then disconnect without motion\n"
    "  --help            Show this help\n"
    "Without --dry-run or --connect-only this program enables and homes BOTH hands, then moves\n"
    "axes 3..6. Thumbs stay at 0. Clear the workspace before running.\n";

bool number(const char* text, int low, int high, int& result) {
    const std::string value(text);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result >= low && result <= high;
}
bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dry-run") { options.dry_run = true; continue; }
        if (arg == "--connect-only") { options.connect_only = true; continue; }
        if (arg == "--left" || arg == "--right") {
            if (i + 2 >= argc) return false;
            const int side = arg == "--left" ? 0 : 1;
            options.can[side] = argv[++i];
            if (!number(argv[++i], 1, 127, options.node[side])) return false;
            continue;
        }
        if (i + 1 >= argc) return false;
        const char* value = argv[++i];
        bool ok = false;
        if (arg == "--rate") ok = number(value, 50, 200, options.rate)
            && (options.rate == 50 || options.rate == 100 || options.rate == 150 || options.rate == 200);
        else if (arg == "--speed") ok = number(value, 1, 20000, options.speed);
        else if (arg == "--current") ok = number(value, 0, 3000, options.current);
        else if (arg == "--amplitude") ok = number(value, 1, 10000, options.amplitude);
        else if (arg == "--period-ms") ok = number(value, 1000, 60000, options.period_ms);
        else if (arg == "--cycles") ok = number(value, 1, 100, options.cycles);
        if (!ok) return false;
    }
    return options.can[0] != options.can[1] || options.node[0] != options.node[1];
}
std::array<int, 6> target(double seconds, int side, const Options& options) {
    constexpr double pi = 3.14159265358979323846;
    const double period = options.period_ms / 1000.0;
    const double duration = period * options.cycles;
    const double ramp = std::min(2.0, duration / 4.0);
    const double edge = std::clamp(std::min(seconds, duration - seconds) / ramp, 0.0, 1.0);
    const double envelope = 0.5 - 0.5 * std::cos(pi * edge);
    std::array<int, 6> values{};
    for (int j = 0; j < 4; ++j) {
        const double phase = 2 * pi * seconds / period + (side == 0 ? -1 : 1) * j * pi / 2;
        values[j + 2] = static_cast<int>(std::lround(options.amplitude * envelope * (1 - std::cos(phase)) / 2));
    }
    return values;
}
void print_values(const std::array<int, 6>& values) {
    for (int value : values) std::cout << ' ' << value;
}
void print_state(const char* side, const dh116s::HandState& state) {
    std::cout << side << " state positions:";
    print_values(state.positions);
    std::cout << " velocities:";
    print_values(state.velocities);
    std::cout << " currents:";
    print_values(state.currents);
    std::cout << " enabled:";
    for (bool value : state.enabled) std::cout << ' ' << value;
    std::cout << " homed:";
    for (bool value : state.homed) std::cout << ' ' << value;
    std::cout << " torque_reached:";
    for (bool value : state.torque_reached) std::cout << ' ' << value;
    std::cout << " faults:";
    print_values(state.fault_codes);
    std::cout << " feedback_valid:" << state.feedback_valid << '\n';
}
bool healthy(const dh116s::HandState& state) {
    return state.feedback_valid
        && std::all_of(state.enabled.begin(), state.enabled.end(), [](bool v) { return v; })
        && std::all_of(state.homed.begin(), state.homed.end(), [](bool v) { return v; })
        && std::all_of(state.fault_codes.begin(), state.fault_codes.end(), [](int v) { return v == 0; });
}
bool cleanup(std::array<dh116s::Hand, 2>& hands) {
    auto close = [](dh116s::Hand& hand) {
        bool ok = true;
        if (hand.is_connected()) {
            ok = hand.disable();
        }
        hand.disconnect();
        return ok;
    };
    auto left = std::async(std::launch::async, [&] { return close(hands[0]); });
    const bool right = close(hands[1]);
    return left.get() && right;
}
bool phase(std::array<dh116s::Hand, 2>& hands, const char* name,
           const std::function<bool(dh116s::Hand&, int)>& operation, bool motion_cleanup = true) {
    if (interrupted) return false;
    std::cout << name << "..." << std::endl;
    auto left = std::async(std::launch::async, [&] { return operation(hands[0], 0); });
    auto right = std::async(std::launch::async, [&] { return operation(hands[1], 1); });
    bool cancelled = false;
    while (left.wait_for(20ms) != std::future_status::ready || right.wait_for(0ms) != std::future_status::ready) {
        if (interrupted && !cancelled) {
            cancelled = true;
            if (motion_cleanup) {
                if (!cleanup(hands)) std::cerr << "Disable not confirmed during interruption.\n";
            } else {
                for (auto& hand : hands) hand.disconnect();
            }
        }
    }
    const bool left_ok = left.get(), right_ok = right.get();
    if (!left_ok || !right_ok) std::cerr << name << " failed: left=" << left_ok << " right=" << right_ok << '\n';
    return left_ok && right_ok && !interrupted;
}
bool run(std::array<dh116s::Hand, 2>& hands, const Options& options) {
    if (!phase(hands, "Connect", [&](auto& hand, int side) { return hand.connect(options.can[side], options.node[side]); }, !options.connect_only)) return false;
    if (options.connect_only) return hands[0].get_state().feedback_valid && hands[1].get_state().feedback_valid;
    if (!phase(hands, "Set speed/current/torque-reached stop mode", [&](auto& hand, int) {
        return hand.set_speed(options.speed) && hand.set_current_limit(options.current) && hand.set_torque_reached_mode(true);
    })) return false;
    if (!phase(hands, "Enable", [](auto& hand, int) { return hand.enable(); })) return false;
    if (!phase(hands, "Home all six axes", [](auto& hand, int) { return hand.home(); })) return false;
    if (!phase(hands, "Single-axis API: index target 0", [](auto& hand, int) { return hand.set_position(3, 0); })) return false;
    std::cout << "Wave: axes 3..6, opposite phase directions, " << options.rate << " Hz targets" << std::endl;
    const double duration = options.period_ms / 1000.0 * options.cycles;
    const auto start = Clock::now();
    auto next = start;
    auto next_report = start;
    while (!interrupted) {
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        if (seconds >= duration) break;
        for (int side = 0; side < 2; ++side) {
            const auto state = hands[side].get_state();
            if (!healthy(state)) {
                std::cerr << "Invalid/disabled/unhomed/fault feedback, side=" << side << " faults:";
                for (int fault : state.fault_codes) std::cerr << ' ' << fault;
                std::cerr << '\n';
                return false;
            }
            if (!hands[side].set_positions(target(seconds, side, options))) return false;
            if (Clock::now() >= next_report) {
                print_state(side == 0 ? "left" : "right", state);
                if (side == 1) next_report += 1s;
            }
        }
        next = std::max(next + std::chrono::nanoseconds(1000000000LL / options.rate), Clock::now());
        std::this_thread::sleep_until(next);
    }
    if (interrupted) return false;
    if (!phase(hands, "Return targets to zero", [](auto& hand, int) { return hand.set_positions({0, 0, 0, 0, 0, 0}); })) return false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(3000 + 1000 * options.amplitude / options.speed);
    while (!interrupted && Clock::now() < deadline) {
        bool arrived = true;
        for (auto& hand : hands) {
            const auto state = hand.get_state();
            if (!healthy(state)) return false;
            for (int j = 0; j < 6; ++j) arrived &= std::abs(state.positions[j]) <= 150 && std::abs(state.velocities[j]) <= 50;
        }
        if (arrived) return true;
        std::this_thread::sleep_for(20ms);
    }
    std::cerr << "Return-to-zero feedback not confirmed.\n";
    return false;
}
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--help") { std::cout << help; return 0; }
    Options options;
    if (!parse(argc, argv, options)) { std::cerr << "Invalid arguments.\n" << help; return 2; }
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    if (options.dry_run) {
        const int ticks = options.period_ms * options.cycles * options.rate / 1000;
        for (int tick = 0; tick <= ticks; ++tick) {
            const double seconds = tick / static_cast<double>(options.rate);
            const auto left = target(seconds, 0, options), right = target(seconds, 1, options);
            for (int side = 0; side < 2; ++side) {
                const auto& q = side ? right : left;
                if (q[0] || q[1]) return 1;
                for (int v : q) if (v < 0 || v > options.amplitude) return 1;
            }
            if (tick % options.rate == 0) {
                std::cout << "t=" << seconds << "s left:"; print_values(left);
                std::cout << " right:"; print_values(right); std::cout << '\n';
            }
        }
        std::cout << "Dry run passed; no CAN connection or hardware commands.\n";
        return 0;
    }
    std::array<dh116s::Hand, 2> hands;
    bool ok = false;
    try { ok = run(hands, options); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    bool closed = true;
    if (options.connect_only) {
        std::cout << "Disconnect (connection check only; no motion commands)" << std::endl;
        for (auto& hand : hands) hand.disconnect();
    } else {
        std::cout << "Disable -> disconnect (both hands)" << std::endl;
        closed = cleanup(hands);
    }
    if (!closed) std::cerr << "Disable unconfirmed; check hardware and power state.\n";
    if (interrupted) return 130;
    std::cout << (ok && closed ? "Complete.\n" : "Failed; see preceding logs.\n");
    return ok && closed ? 0 : 1;
}
